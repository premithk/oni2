/*
 * ExtHostClient.re
 *
 * This is a client-side API for integrating with our VSCode extension host API.
 *
 */

open Oni_Core;
open Reason_jsonrpc;
open Rench;
/* open Revery; */

module Protocol = ExtHostProtocol;

type t = {
  process: NodeProcess.t,
  rpc: Rpc.t,
  send: (int, Yojson.Safe.t) => unit,
};

let emptyJsonValue = `Assoc([]);

type simpleCallback = unit => unit;
let defaultCallback: simpleCallback = () => ();

type messageHandler =
  (string, string, list(Yojson.Safe.t)) =>
  result(option(Yojson.Safe.t), string);
let defaultMessageHandler = (_, _, _) => Ok(None);

let start =
    (
      ~initData=ExtHostInitData.create(),
      ~onInitialized=defaultCallback,
      ~onMessage=defaultMessageHandler,
      ~onClosed=defaultCallback,
      setup: Setup.t,
    ) => {
  let args = ["--type=extensionHost"];
  let env = [
    "AMD_ENTRYPOINT=vs/workbench/services/extensions/node/extensionHostProcess",
    "VSCODE_PARENT_PID=" ++ string_of_int(Process.pid()),
  ];

  let process =
    NodeProcess.start(
      ~args,
      ~env,
      setup,
      Setup.getNodeExtensionHostPath(setup),
    );

  let lastReqId = ref(0);
  let rpcRef = ref(None);
  let initialized = ref(false);
  let queuedCallbacks = ref([]);

  let queue = f => {
    queuedCallbacks := [f, ...queuedCallbacks^];
  };

  let send = (msgType, msg: Yojson.Safe.t) => {
    switch (rpcRef^) {
    | None => Log.error("ExtHostClient: RPC not initialized.")
    | Some(v) =>
      incr(lastReqId);
      let reqId = lastReqId^;

      let request =
        `Assoc([
          ("type", `Int(msgType)),
          ("reqId", `Int(reqId)),
          ("payload", msg),
        ]);

      Rpc.sendNotification(v, "ext/msg", request);
    };
  };

  let sendRequest = (msg: Yojson.Safe.t) => {
    send(ExtHostProtocol.MessageType.requestJsonArgs, msg);
  };

  let sendResponse = (msgType, reqId, msg) => {
    switch (rpcRef^) {
    | None => Log.error("ExtHostClient: RPC not initialized.")
    | Some(v) =>
      let response =
        `Assoc([
          ("type", `Int(msgType)),
          ("reqId", `Int(reqId)),
          ("payload", msg),
        ]);
      Rpc.sendNotification(v, "ext/msg", response);
    };
  };

  let handleMessage = (reqId: int, payload: Yojson.Safe.t) =>
    switch (payload) {
    | `Assoc([
        ("rpcName", `String(scopeName)),
        ("methodName", `String(methodName)),
        ("args", `List(args)),
      ]) =>
      switch (onMessage(scopeName, methodName, args)) {
      | Ok(None) => sendResponse(12, reqId, `Assoc([]))
      | _ => sendResponse(12, reqId, `Assoc([]))
      }
    | _ =>
      Log.error(
        "ExtHostTransport - Unknown message: "
        ++ Yojson.Safe.to_string(payload),
      )
    };

  let _sendInitData = () => {
    send(Protocol.MessageType.initData, ExtHostInitData.to_yojson(initData));
  };

  let _handleInitialization = () => {
    onInitialized();
    /* Send workspace and configuration info to get the extensions started */
    open ExtHostProtocol.OutgoingNotifications;

    Configuration.initializeConfiguration() |> sendRequest;
    Workspace.initializeWorkspace("onivim-workspace-id", "onivim-workspace")
    |> sendRequest;

    initialized := true;

    queuedCallbacks^ |> List.rev |> List.iter(f => f());
  };

  let onNotification = (n: Notification.t, _) => {
    switch (n.method, n.params) {
    | ("host/msg", json) =>
      Protocol.Notification.(
        switch (parse(json)) {
        | Request(req) => handleMessage(req.reqId, req.payload)
        | Reply(_) => ()
        | Ack(_) => ()
        | Error => ()
        | Ready => _sendInitData()
        | Initialized => _handleInitialization()
        }
      )

    | _ => Log.error("[Extension Host Client] Unknown message: " ++ n.method)
    };
  };

  let onRequest = (_, _) => Ok(emptyJsonValue);

  let rpc =
    Rpc.start(
      ~onNotification,
      ~onRequest,
      ~onClose=onClosed,
      process.stdout,
      process.stdin,
    );

  rpcRef := Some(rpc);

  let wrappedSend = (msgType, msg) => {
    let f = () => send(msgType, msg);
    initialized^ ? f() : queue(f);
  };

  {process, rpc, send: wrappedSend};
};

let pump = (v: t) => Rpc.pump(v.rpc);

let send =
    (
      v: t,
      ~msgType=ExtHostProtocol.MessageType.requestJsonArgs,
      msg: Yojson.Safe.t,
    ) => {
  v.send(msgType, msg);
};

let close = (v: t) => {
  v.send(ExtHostProtocol.MessageType.terminate, `Assoc([]));
};
