/*
 * MenuStoreConnector.re
 *
 * This implements an updater (reducer + side effects) for the Menu
 */

module Core = Oni_Core;
module Model = Oni_Model;

module Actions = Model.Actions;
module Animation = Model.Animation;
module Menu = Model.Menu;
module MenuJob = Model.MenuJob;

let start = () => {
  let (stream, dispatch) = Isolinear.Stream.create();

  let position = (selectedItem, change, count) => {
    let nextIndex = selectedItem + change;

    if (nextIndex >= count) {
      0;
    } else if (nextIndex < 0) {
      count - 1;
    } else {
      nextIndex;
    };
  };

  let menuOpenEffect =
      (menuConstructor, onQueryChangedEvent, onSelectedItemChangedEvent) =>
    Isolinear.Effect.create(~name="menu.construct", () => {
      let setItems = items => dispatch(Actions.MenuUpdate(items));
      let startTime = Revery.Time.getTime() |> Revery.Time.toSeconds;
      let setLoading = isLoading =>
        dispatch(Actions.MenuSetLoading(isLoading, startTime));

      let disposeFunction =
        menuConstructor(
          setItems,
          onQueryChangedEvent,
          onSelectedItemChangedEvent,
          setLoading,
        );
      dispatch(Actions.MenuSetDispose(disposeFunction));
    });

  let queryChangedEffect = (evt, newQuery) =>
    Isolinear.Effect.create(~name="menu.queryChanged", () =>
      Rench.Event.dispatch(evt, newQuery)
    );

  let selectedItemChangedEffect = (evt, newItem) =>
    Isolinear.Effect.create(~name="menu.selectedItemChanged", () => {
      Rench.Event.dispatch(evt, newItem)
    });

  let selectItemEffect = command =>
    Isolinear.Effect.createWithDispatch(~name="menu.selectItem", dispatch => {
      let action = command();
      dispatch(action);
    });

  let disposeMenuEffect = dispose =>
    Isolinear.Effect.create(~name="menu.dispose", dispose);

  let rec menuUpdater = (state: Menu.t, action: Actions.t) => {
    let filteredCommands =
      Core.Job.getCompletedWork(state.filterJob).uiFiltered;
    let getSelectedItem = idx =>
      switch (filteredCommands[idx]) {
      | exception (Invalid_argument(_)) => None
      | v => Some(v)
      };
    let filteredCommandsCount = filteredCommands |> Array.length;
    switch (action) {
    | MenuSetLoading(isLoading, time) => (
        {
          ...state,
          isLoading,
          loadingAnimation: Animation.start(time, state.loadingAnimation),
        },
        Isolinear.Effect.none,
      )
    | MenuPosition(index) => (
        {...state, selectedItem: index},
        Isolinear.Effect.none,
      )
    | MenuPreviousItem =>
      let selectedItem =
        position(state.selectedItem, -1, filteredCommandsCount);
      let item = getSelectedItem(selectedItem);
      let effect =
        selectedItemChangedEffect(state.onSelectedItemChanged, item);
      ({...state, selectedItem}, effect);
    | MenuNextItem =>
      let selectedItem =
        position(state.selectedItem, 1, filteredCommandsCount);
      let item = getSelectedItem(selectedItem);
      let effect =
        selectedItemChangedEffect(state.onSelectedItemChanged, item);
      ({...state, selectedItem}, effect);
    | MenuSearch(query) => (
        {
          ...state,
          searchQuery: query,
          filterJob:
            Core.Job.mapw(MenuJob.updateQuery(query), state.filterJob),
          selectedItem:
            position(state.selectedItem, 0, filteredCommandsCount),
        },
        queryChangedEffect(state.onQueryChanged, query),
      )
    | MenuOpen(menuConstructor) =>
      let state = Menu.create();
      (
        {...state, isOpen: true},
        menuOpenEffect(
          menuConstructor,
          state.onQueryChanged,
          state.onSelectedItemChanged,
        ),
      );
    | MenuUpdate(update) =>
      let filterJob =
        Core.Job.mapw(MenuJob.addItems(update), state.filterJob);
      let selectedItem =
        position(state.selectedItem, 0, filteredCommandsCount);

      ({...state, filterJob, selectedItem}, Isolinear.Effect.none);
    | MenuSetDispose(dispose) => (
        {...state, dispose},
        Isolinear.Effect.none,
      )
    | MenuClose =>
      let disposeFunction = state.dispose;
      (
        {
          ...state,
          filterJob: MenuJob.default,
          isOpen: false,
          selectedItem: 0,
          isLoading: false,
          loadingAnimation: Animation.stop(state.loadingAnimation),
        },
        disposeMenuEffect(disposeFunction),
      );
    | MenuSelect =>
      let effect =
        switch (getSelectedItem(state.selectedItem)) {
        | None => Isolinear.Effect.none
        | Some(v) => selectItemEffect(v.command)
        };

      /* Also close menu */
      let (closeState, closeEffect) = menuUpdater(state, MenuClose);

      (closeState, Isolinear.Effect.batch([closeEffect, effect]));
    | _ => (state, Isolinear.Effect.none)
    };
  };

  let updateJob = (state: Model.State.t) =>
    if (Core.Job.isComplete(state.menu.filterJob)) {
      state;
    } else {
      {
        ...state,
        menu: {
          ...state.menu,
          filterJob: Core.Job.tick(state.menu.filterJob),
        },
      };
    };

  let updateAnimation = (deltaT: float, state: Model.State.t) =>
    if (state.menu.isLoading) {
      {
        ...state,
        menu: {
          ...state.menu,
          loadingAnimation:
            Animation.tick(deltaT, state.menu.loadingAnimation),
        },
      };
    } else {
      state;
    };

  let updater = (state: Model.State.t, action: Actions.t) =>
    switch (action) {
    | Actions.Tick({deltaTime, _}) =>
      let newState = state |> updateJob |> updateAnimation(deltaTime);

      (newState, Isolinear.Effect.none);
    | action =>
      let (menuState, menuEffect) = menuUpdater(state.menu, action);
      let state = {...state, menu: menuState};
      (state, menuEffect);
    };

  (updater, stream);
};
