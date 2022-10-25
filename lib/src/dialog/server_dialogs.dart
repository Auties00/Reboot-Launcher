import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';
import 'package:reboot_launcher/src/dialog/dialog.dart';
import 'package:reboot_launcher/src/dialog/dialog_button.dart';
import 'package:reboot_launcher/src/dialog/snackbar.dart';
import 'package:reboot_launcher/src/model/server_type.dart';
import 'package:reboot_launcher/src/util/future.dart';
import 'package:sync/semaphore.dart';

import '../../main.dart';
import '../util/server.dart';

extension ServerControllerDialog on ServerController {
  static Semaphore semaphore = Semaphore();

  Future<bool> changeStateInteractive(bool onlyIfNeeded, [bool isRetry = false]) async {
    try{
      semaphore.acquire();
      if (type() == ServerType.local) {
        return _checkLocalServerInteractive(onlyIfNeeded);
      }

      var oldStarted = started();
      if(oldStarted && onlyIfNeeded){
        return true;
      }

      started.value = !started.value;
      return await _doStateChange(oldStarted, onlyIfNeeded, isRetry);
    }finally{
      semaphore.release();
    }
  }

  Future<bool> _doStateChange(bool oldStarted, bool onlyIfNeeded, bool isRetry) async {
    if (oldStarted) {
      var result = await stop();
      if (!result) {
        started.value = true;
        _showCannotStopError();
        return true;
      }

      return false;
    }

    var result = await start(!onlyIfNeeded);
    if(result.type == ServerResultType.ignoreStart) {
      started.value = false;
      return true;
    }

    var handled = await _handleResultType(oldStarted, onlyIfNeeded, isRetry, result);
    if (!handled) {
      started.value = false;
      return false;
    }

    return handled;
  }

  Future<bool> _handleResultType(bool oldStarted, bool onlyIfNeeded, bool isRetry, ServerResult result) async {
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
        if(!started() || result.pid != embeddedServer?.pid){
          return false;
        }

        _showPingErrorDialog();
        return false;
      case ServerResultType.portTakenError:
        if (isRetry) {
          _showPortTakenError();
          return false;
        }

        var result = await _showPortTakenDialog();
        if (!result) {
          return false;
        }

        await freeLawinPort();
        return _doStateChange(oldStarted, onlyIfNeeded, true);
      case ServerResultType.serverDownloadRequiredError:
        if (isRetry) {
          return false;
        }

        var result = await downloadServerInteractive(false);
        if (!result) {
          return false;
        }

        return _doStateChange(oldStarted, onlyIfNeeded, true);
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
      case ServerResultType.ignoreStart:
      case ServerResultType.started:
        return true;
      case ServerResultType.canStart:
      case ServerResultType.stopped:
        return false;
    }
  }

  Future<bool> _checkLocalServerInteractive(bool ignorePrompts) async {
    try {
      var future = pingSelf(port.text);
      if(!ignorePrompts) {
        await showDialog(
            context: appKey.currentContext!,
            builder: (context) =>
                FutureBuilderDialog(
                    future: future,
                    loadingMessage: "Pinging server...",
                    loadedBody: FutureBuilderDialog.ofMessage(
                        "The server at ${host.text}:${port
                            .text} works correctly"),
                    errorMessageBuilder: (
                        exception) => "An error occurred while pining the server: $exception"
                )
        );
      }
      return await future != null;
    } catch (_) {
      return false;
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

  void _showPingErrorDialog() {
    if(!started.value){
      return;
    }

    showDialog(
        context: appKey.currentContext!,
        builder: (context) =>
        const InfoDialog(
            text: "The lawin server is not working correctly. Check the configuration in the associated tab and try again."
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
            text: "Cannot stop lawin server"
        )
    );
  }

  void showUnexpectedError() {
    showDialog(
        context: appKey.currentContext!,
        builder: (context) =>
        const InfoDialog(
            text: "The lawin server died unexpectedly"
        )
    );
  }

  void _showIllegalPortError() {
    showMessage("Illegal port for lawin server, use only numbers");
  }

  void _showMissingPortError() {
    showMessage("Missing port for lawin server");
  }

  void _showMissingHostError() {
    showMessage("Missing the host name for lawin server");
  }
}

Future<bool> downloadServerInteractive(bool closeAutomatically) async {
  var download = compute(downloadServer, true);
  return await showDialog<bool>(
      context: appKey.currentContext!,
      builder: (context) =>
          FutureBuilderDialog(
              future: download,
              loadingMessage: "Downloading server...",
              loadedBody: FutureBuilderDialog.ofMessage(
                  "The server was downloaded successfully"),
              errorMessageBuilder: (
                  message) => "Cannot download server: $message",
              closeAutomatically: closeAutomatically
          )
  ) ?? download.isCompleted();
}