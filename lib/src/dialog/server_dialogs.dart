import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/dialog/dialog.dart';
import 'package:reboot_launcher/src/dialog/dialog_button.dart';
import 'package:reboot_launcher/src/dialog/snackbar.dart';
import 'package:reboot_launcher/src/embedded/server.dart';
import 'package:reboot_launcher/src/model/server_type.dart';
import 'package:sync/semaphore.dart';
import 'package:url_launcher/url_launcher.dart';

import '../../main.dart';
import '../util/server.dart';

extension ServerControllerDialog on ServerController {
  static Semaphore semaphore = Semaphore();

  Future<bool> start({required bool required, required bool askPortKill, bool isRetry = false}) async {
    try{
      semaphore.acquire();
      if (type() == ServerType.local) {
        return _pingSelfInteractive(required);
      }

      var oldStarted = started();
      if(oldStarted && required){
        return true;
      }

      started.value = !started.value;
      var result = await _startInternal(oldStarted, required, askPortKill, isRetry);
      if(!result){
       return false;
      }

     return await _pingSelfInteractive(true);
    }finally{
      semaphore.release();
    }
  }

  Future<bool> _startInternal(bool oldStarted, bool required, bool askPortKill, bool isRetry) async {
    if (oldStarted) {
      var result = await stop();
      if (!result) {
        started.value = true;
        _showCannotStopError();
        return true;
      }

      return false;
    }

    var conditions = await checkServerPreconditions(host.text, port.text, type.value, !required);
    var result = conditions.type == ServerResultType.canStart ? await _startServer(required) : conditions;
    if(result.type == ServerResultType.alreadyStarted) {
      started.value = false;
      return true;
    }

    var handled = await _handleResultType(oldStarted, required, isRetry, askPortKill, result);
    if (!handled) {
      started.value = false;
      return false;
    }

    return handled;
  }

  Future<ServerResult> _startServer(bool closeAutomatically) async {
    try{
      switch(type()){
        case ServerType.embedded:
          embeddedServer = await startEmbeddedServer(
                  () => Get.find<SettingsController>().matchmakingIp.text,
          );
          embeddedMatchmaker = await startEmbeddedMatchmaker();
          break;
        case ServerType.remote:
          var uriResult = await _pingRemoteInteractive(closeAutomatically);
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

  Future<bool> _handleResultType(bool oldStarted, bool onlyIfNeeded, bool isRetry, bool askPortKill, ServerResult result) async {
    switch (result.type) {
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
      case ServerResultType.portTakenError:
        if (isRetry) {
          _showPortTakenError();
          return false;
        }

        if(askPortKill) {
          var result = await _showPortTakenDialog();
          if (!result) {
            return false;
          }
        }

        await freeLawinPort();
        return _startInternal(oldStarted, onlyIfNeeded, askPortKill, true);
      case ServerResultType.unknownError:
        showDialog(
            context: appKey.currentContext!,
            builder: (context) =>
                ErrorDialog(
                    exception: result.error ?? Exception("Unknown error"),
                    stackTrace: result.stackTrace,
                    errorMessageBuilder: (
                        exception) => "Cannot start server: $exception"
                )
        );
        return false;
      case ServerResultType.alreadyStarted:
      case ServerResultType.canStart:
        return true;
      case ServerResultType.stopped:
        return false;
    }
  }

  Future<bool> _pingSelfInteractive(bool closeAutomatically) async {
    try {
      return await showDialog<bool>(
          context: appKey.currentContext!,
          builder: (context) =>
              FutureBuilderDialog(
                  future: Future.wait([
                    pingSelf(port.text),
                    Future.delayed(const Duration(seconds: 1))
                  ]),
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
    } catch (_) {
      return false;
    }
  }

  Future<Uri?> _pingRemoteInteractive(bool closeAutomatically) async {
    try {
      var mainFuture = ping(host.text, port.text);
      var result = await showDialog<bool>(
          context: appKey.currentContext!,
          builder: (context) =>
              FutureBuilderDialog(
                  future: Future.wait([
                    mainFuture,
                    Future.delayed(const Duration(seconds: 1))
                  ]),
                  loadingMessage: "Pinging remote server...",
                  successfulBody: FutureBuilderDialog.ofMessage(
                      "The server at ${host.text}:${port
                          .text} works correctly"),
                  unsuccessfulBody: FutureBuilderDialog.ofMessage(
                      "The server at ${host.text}:${port
                          .text} doesn't work. Check the hostname and/or the port and try again."),
                  errorMessageBuilder: (exception) => "An error occurred while pining the server: $exception",
                  closeAutomatically: closeAutomatically
              )
      ) ?? false;
      return result ? await mainFuture : null;
    } catch (_) {
      return null;
    }
  }

  Future<void> _showPortTakenError() async {
    showDialog(
        context: appKey.currentContext!,
        builder: (context) =>
        const InfoDialog(
          text: "Port 3551 is already in use and the associating process cannot be killed. Kill it manually and try again.",
        )
    );
  }

  Future<bool> _showPortTakenDialog() async {
    return await showDialog<bool>(
        context: appKey.currentContext!,
        builder: (context) =>
            InfoDialog(
              text: "Port 3551 is already in use, do you want to kill the associated process?",
              buttons: [
                DialogButton(
                  type: ButtonType.secondary,
                  onTap: () => Navigator.of(context).pop(false),
                ),
                DialogButton(
                  text: "Kill",
                  type: ButtonType.primary,
                  onTap: () => Navigator.of(context).pop(true),
                ),
              ],
            )
    ) ?? false;
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

  void showUnexpectedServerError() {
    showDialog(
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
                launchUrl(serverLogFile.uri);
                Navigator.of(context).pop();
              }
            ),
          ],
        )
    );
  }

  void _showIllegalPortError() {
    showMessage("Illegal port for backend server, use only numbers");
  }

  void _showMissingPortError() {
    showMessage("Missing port for backend server");
  }

  void _showMissingHostError() {
    showMessage("Missing the host name for backend server");
  }
}