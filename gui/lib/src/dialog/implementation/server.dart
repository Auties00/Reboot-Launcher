import 'dart:async';

import 'package:clipboard/clipboard.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/material.dart' show Icons;
import 'package:get/get_rx/src/rx_types/rx_types.dart';
import 'package:get/get_state_manager/src/rx_flutter/rx_obx_widget.dart';
import 'package:reboot_launcher/src/controller/matchmaker_controller.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';
import 'package:reboot_launcher/src/dialog/abstract/dialog.dart';
import 'package:reboot_launcher/src/dialog/abstract/dialog_button.dart';
import 'package:reboot_launcher/src/dialog/abstract/info_bar.dart';

import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/page/home_page.dart';
import 'package:reboot_launcher/src/util/cryptography.dart';
import 'package:reboot_launcher/src/util/matchmaker.dart';

extension ServerControllerDialog on ServerController {
  Future<bool> restartInteractive() async {
    var stream = restart();
    return await _handleStream(stream, false);
  }

  Future<bool> toggleInteractive([bool showSuccessMessage = true]) async {
    var stream = toggle();
    return await _handleStream(stream, showSuccessMessage);
  }

  Future<bool> _handleStream(Stream<ServerResult> stream, bool showSuccessMessage) async {
    var completer = Completer<bool>();
    stream.listen((event) {
      switch (event.type) {
        case ServerResultType.starting:
          showInfoBar(
              "Starting the $controllerName...",
              severity: InfoBarSeverity.info,
              loading: true,
              duration: null
          );
          break;
        case ServerResultType.startSuccess:
          if(showSuccessMessage) {
            showInfoBar(
                "The $controllerName was started successfully",
                severity: InfoBarSeverity.success
            );
          }
          completer.complete(true);
          break;
        case ServerResultType.startError:
          showInfoBar(
              "An error occurred while starting the $controllerName: ${event.error ?? "unknown error"}",
              severity: InfoBarSeverity.error,
              duration: snackbarLongDuration
          );
          break;
        case ServerResultType.stopping:
          showInfoBar(
              "Stopping the $controllerName...",
              severity: InfoBarSeverity.info,
              loading: true,
              duration: null
          );
          break;
        case ServerResultType.stopSuccess:
          if(showSuccessMessage) {
            showInfoBar(
                "The $controllerName was stopped successfully",
                severity: InfoBarSeverity.success
            );
          }
          completer.complete(true);
          break;
        case ServerResultType.stopError:
          showInfoBar(
              "An error occurred while stopping the $controllerName: ${event.error ?? "unknown error"}",
              severity: InfoBarSeverity.error,
              duration: snackbarLongDuration
          );
          break;
        case ServerResultType.missingHostError:
          showInfoBar(
              "Missing hostname in $controllerName configuration",
              severity: InfoBarSeverity.error
          );
          break;
        case ServerResultType.missingPortError:
          showInfoBar(
              "Missing port in $controllerName configuration",
              severity: InfoBarSeverity.error
          );
          break;
        case ServerResultType.illegalPortError:
          showInfoBar(
              "Invalid port in $controllerName configuration",
              severity: InfoBarSeverity.error
          );
          break;
        case ServerResultType.freeingPort:
          showInfoBar(
              "Freeing port $defaultPort...",
              loading: true,
              duration: null
          );
          break;
        case ServerResultType.freePortSuccess:
          showInfoBar(
              "Port $defaultPort was freed successfully",
              severity: InfoBarSeverity.success,
              duration: snackbarShortDuration
          );
          break;
        case ServerResultType.freePortError:
          showInfoBar(
              "An error occurred while freeing port $defaultPort: ${event.error ?? "unknown error"}",
              severity: InfoBarSeverity.error,
              duration: snackbarLongDuration
          );
          break;
        case ServerResultType.pingingRemote:
          if(started.value) {
            showInfoBar(
                "Pinging the remote $controllerName...",
                severity: InfoBarSeverity.info,
                loading: true,
                duration: null
            );
          }
          break;
        case ServerResultType.pingingLocal:
          if(started.value) {
            showInfoBar(
                "Pinging the ${type().name} $controllerName...",
                severity: InfoBarSeverity.info,
                loading: true,
                duration: null
            );
          }
          break;
        case ServerResultType.pingError:
          showInfoBar(
              "Cannot ping ${type().name} $controllerName",
              severity: InfoBarSeverity.error
          );
          break;
      }

      if(event.type.isError) {
        completer.complete(false);
      }
    });

    var result = await completer.future;
    if(result && type() == ServerType.embedded) {
      watchProcess(embeddedServerPid!).then((value) {
        if(started()) {
          started.value = false;
        }
      });
    }

    return result;
  }
}

extension MatchmakerControllerExtension on MatchmakerController {
  Future<void> joinServer(Map<String, dynamic> entry) async {
    var hashedPassword = entry["password"];
    var hasPassword = hashedPassword != null;
    var embedded = type.value == ServerType.embedded;
    var author = entry["author"];
    var encryptedIp = entry["ip"];
    if(!hasPassword) {
      var valid = await _isServerValid(encryptedIp);
      if(!valid) {
        return;
      }

      _onSuccess(embedded, encryptedIp, author);
      return;
    }

    var confirmPassword = await _askForPassword();
    if(confirmPassword == null) {
      return;
    }

    if(!checkPassword(confirmPassword, hashedPassword)) {
      showInfoBar(
          "Wrong password: please try again",
          duration: snackbarLongDuration,
          severity: InfoBarSeverity.error
      );
      return;
    }

    var decryptedIp = aes256Decrypt(encryptedIp, confirmPassword);
    var valid = await _isServerValid(decryptedIp);
    if(!valid) {
      return;
    }

    _onSuccess(embedded, decryptedIp, author);
  }

  Future<bool> _isServerValid(String address) async {
    var result = await pingGameServer(address);
    if(result) {
      return true;
    }

    showInfoBar(
        "This server isn't online right now: please try again later",
        duration: snackbarLongDuration,
        severity: InfoBarSeverity.error
    );
    return false;
  }

  Future<String?> _askForPassword() async {
    var confirmPasswordController = TextEditingController();
    var showPassword = RxBool(false);
    var showPasswordTrailing = RxBool(false);
    return await showAppDialog<String?>(
        builder: (context) => FormDialog(
            content: Column(
              mainAxisSize: MainAxisSize.min,
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                InfoLabel(
                    label: "Password",
                    child: Obx(() => TextFormBox(
                        placeholder: "Type the server's password",
                        controller: confirmPasswordController,
                        autovalidateMode: AutovalidateMode.always,
                        obscureText: !showPassword.value,
                        enableSuggestions: false,
                        autofocus: true,
                        autocorrect: false,
                        onChanged: (text) => showPasswordTrailing.value = text.isNotEmpty,
                        suffix: Button(
                          onPressed: () => showPasswordTrailing.value = !showPasswordTrailing.value,
                          style: ButtonStyle(
                              shape: ButtonState.all(const CircleBorder()),
                              backgroundColor: ButtonState.all(Colors.transparent)
                          ),
                          child: Icon(
                              showPassword.value ? Icons.visibility_off : Icons.visibility,
                              color: showPassword.value ? null : Colors.transparent
                          ),
                        )
                    ))
                ),
                const SizedBox(height: 8.0)
              ],
            ),
            buttons: [
              DialogButton(
                  text: "Cancel",
                  type: ButtonType.secondary
              ),

              DialogButton(
                  text: "Confirm",
                  type: ButtonType.primary,
                  onTap: () => Navigator.of(context).pop(confirmPasswordController.text)
              )
            ]
        )
    );
  }

  void _onSuccess(bool embedded, String decryptedIp, String author) {
    if(embedded) {
      gameServerAddress.text = decryptedIp;
      pageIndex.value = 0;
    }else {
      FlutterClipboard.controlC(decryptedIp);
    }
    WidgetsBinding.instance.addPostFrameCallback((_) => showInfoBar(
        embedded ? "You joined $author's server successfully!" : "Copied IP to the clipboard",
        duration: snackbarLongDuration,
        severity: InfoBarSeverity.success
    ));
  }
}