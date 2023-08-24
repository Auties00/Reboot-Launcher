import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/model/server_type.dart';
import 'package:reboot_launcher/src/ui/dialog/snackbar.dart';
import 'package:sync/semaphore.dart';
import 'package:url_launcher/url_launcher.dart';

import 'package:reboot_launcher/main.dart';
import 'package:reboot_launcher/src/util/server.dart';
import 'package:reboot_launcher/src/ui/controller/server_controller.dart';
import 'dialog.dart';
import 'dialog_button.dart';

extension ServerControllerDialog on ServerController {
  static Semaphore semaphore = Semaphore();

  Future<bool> restart(bool closeLocalPromptAutomatically) async {
    await resetWinNat();
    return (!started() || await stop()) && await toggle(closeLocalPromptAutomatically);
  }

  Future<bool> toggle(bool closeLocalPromptAutomatically) async {
    try{
      semaphore.acquire();
      if (type() == ServerType.local) {
        return _pingSelfInteractive(closeLocalPromptAutomatically);
      }

      var result = await _toggle();
      if(!result){
        started.value = false;
        return false;
      }

      var ping = await _pingSelfInteractive(true);
      if(!ping){
        started.value = false;
        return false;
      }

      return true;
    }finally{
      semaphore.release();
    }
  }

  Future<bool> _toggle([ServerResultType? lastResultType]) async {
    if (started.value) {
      var result = await stop();
      if (!result) {
        started.value = true;
        _showCannotStopError();
        return true;
      }

      return false;
    }

    started.value = true;
    var conditions = await checkServerPreconditions(host.text, port.text, type.value);
    var result = conditions.type == ServerResultType.canStart ? await _startServer() : conditions;
    if(result.type == ServerResultType.alreadyStarted) {
      started.value = false;
      return true;
    }

    var handled = await _handleResultType(result, lastResultType);
    if (!handled) {
      return false;
    }

    return handled;
  }

  Future<ServerResult> _startServer() async {
    try{
      switch(type()){
        case ServerType.embedded:
          startServer(detached());
          break;
        case ServerType.remote:
          var uriResult = await _pingRemoteInteractive();
          if(uriResult == null){
            return ServerResult(
                type: ServerResultType.cannotPingServer
            );
          }

          remoteServer = await startRemoteServer(uriResult);
          break;
        case ServerType.local:
          break;
      }
    }catch(error, stackTrace){
      return ServerResult(
          error: error,
          stackTrace: stackTrace,
          type: ServerResultType.unknownError
      );
    }

    return ServerResult(
        type: ServerResultType.canStart
    );
  }

  Future<bool> _handleResultType(ServerResult result, ServerResultType? lastResultType) async {
    var newResultType = result.type;
    switch (newResultType) {
      case ServerResultType.missingHostError:
        _showMissingHostError();
        return false;
      case ServerResultType.missingPortError:
        _showMissingPortError();
        return false;
      case ServerResultType.illegalPortError:
        _showIllegalPortError();
        return false;
      case ServerResultType.cannotPingServer:
        return false;
      case ServerResultType.backendPortTakenError:
        if (lastResultType == ServerResultType.backendPortTakenError) {
          _showPortTakenError(3551);
          return false;
        }

        await freeLawinPort();
        await stop();
        return _toggle(newResultType);
      case ServerResultType.matchmakerPortTakenError:
        if (lastResultType == ServerResultType.matchmakerPortTakenError) {
          _showPortTakenError(8080);
          return false;
        }

        await freeMatchmakerPort();
        await stop();
        return _toggle(newResultType);
      case ServerResultType.unknownError:
        if(lastResultType == ServerResultType.unknownError) {
          _showUnknownError(result);
          return false;
        }

        await resetWinNat();
        await stop();
        return _toggle(newResultType);
      case ServerResultType.alreadyStarted:
      case ServerResultType.canStart:
        return true;
      case ServerResultType.stopped:
        return false;
    }
  }

  Future<bool> _pingSelfInteractive(bool closeAutomatically) async {
    try {
      Future<bool> ping() async {
        for(var i = 0; i < 3; i++){
          var result = await pingSelf(port.text);
          if(result != null){
            return true;
          }else {
            await Future.delayed(const Duration(seconds: 1));
          }
        }

        return false;
      }

      var future = _waitFutureOrTime(ping());
      var result = await showDialog<bool>(
          context: appKey.currentContext!,
          builder: (context) =>
              FutureBuilderDialog(
                  future: future,
                  loadingMessage: "Pinging ${type().id} server...",
                  successfulBody: FutureBuilderDialog.ofMessage(
                      "The ${type().id} server works correctly"),
                  unsuccessfulBody: FutureBuilderDialog.ofMessage(
                      "The ${type().id} server doesn't work. Check the backend tab for misconfigurations and try again."),
                  errorMessageBuilder: (
                      exception) => "An error occurred while pining the ${type().id} server: $exception",
                  closeAutomatically: closeAutomatically
              )
      ) ?? false;
      return result && await future;
    } catch (_) {
      return false;
    }
  }

  Future<Uri?> _pingRemoteInteractive() async {
    try {
      var future = ping(host.text, port.text);
      await showDialog<bool>(
          context: appKey.currentContext!,
          builder: (context) =>
              FutureBuilderDialog(
                  future: future,
                  closeAutomatically: true,
                  loadingMessage: "Pinging remote server...",
                  successfulBody: FutureBuilderDialog.ofMessage(
                      "The server at ${host.text}:${port
                          .text} works correctly"),
                  unsuccessfulBody: FutureBuilderDialog.ofMessage(
                      "The server at ${host.text}:${port
                          .text} doesn't work. Check the hostname and/or the port and try again."),
                  errorMessageBuilder: (exception) => "An error occurred while pining the server: $exception"
              )
      );
      return await future;
    } catch (_) {
      return null;
    }
  }

  Future<void> _showPortTakenError(int port) async {
    showDialog(
        context: appKey.currentContext!,
        builder: (context) => InfoDialog(
          text: "Port $port is already in use and the associating process cannot be killed. Kill it manually and try again.",
        )
    );
  }

  void _showCannotStopError() {
    if(!started.value){
      return;
    }

    showDialog(
        context: appKey.currentContext!,
        builder: (context) =>
        const InfoDialog(
            text: "Cannot stop backend server"
        )
    );
  }

  void showUnexpectedServerError() => showDialog(
      context: appKey.currentContext!,
      builder: (context) => InfoDialog(
        text: "The backend server died unexpectedly",
        buttons: [
          DialogButton(
            text: "Close",
            type: ButtonType.secondary,
            onTap: () => Navigator.of(context).pop(),
          ),

          DialogButton(
              text: "Open log",
              type: ButtonType.primary,
              onTap: () {
                if(serverLogFile.existsSync()){
                  showMessage("No log is available");
                }else {
                  launchUrl(serverLogFile.uri);
                }
                Navigator.of(context).pop();
              }
          ),
        ],
      )
  );

  void _showIllegalPortError() => showMessage("Invalid port for backend server");

  void _showMissingPortError() => showMessage("Missing port for backend server");

  void _showMissingHostError() => showMessage("Missing the host name for backend server");

  Future<Object?> _showUnknownError(ServerResult result) => showDialog(
      context: appKey.currentContext!,
      builder: (context) =>
          ErrorDialog(
              exception: result.error ?? Exception("Unknown error"),
              stackTrace: result.stackTrace,
              errorMessageBuilder: (exception) => "Cannot start the backend: an unknown error occurred"
          )
  );

  Future<dynamic> _waitFutureOrTime(Future<bool> resultFuture) => Future.wait<bool>([resultFuture, Future.delayed(const Duration(seconds: 1)).then((value) => true)]).then((value) => value.reduce((f, s) => f && s));
}