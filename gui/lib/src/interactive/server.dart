import 'dart:async';

import 'package:clipboard/clipboard.dart';
import 'package:fluent_ui/fluent_ui.dart' hide showDialog;
import 'package:flutter/material.dart' show Icons;
import 'package:get/get_rx/src/rx_types/rx_types.dart';
import 'package:get/get_state_manager/src/rx_flutter/rx_obx_widget.dart';
import 'package:reboot_launcher/src/controller/matchmaker_controller.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';
import 'package:reboot_launcher/src/dialog/dialog.dart';
import 'package:reboot_launcher/src/dialog/dialog_button.dart';
import 'package:reboot_launcher/src/dialog/message.dart';

import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/page/home_page.dart';
import 'package:reboot_launcher/src/util/cryptography.dart';

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
        case ServerResultType.missingHostError:
          showMessage(
              "Cannot launch game: missing hostname in $controllerName configuration",
              severity: InfoBarSeverity.error
          );
          break;
        case ServerResultType.missingPortError:
          showMessage(
              "Cannot launch game: missing port in $controllerName configuration",
              severity: InfoBarSeverity.error
          );
          break;
        case ServerResultType.illegalPortError:
          showMessage(
              "Cannot launch game: invalid port in $controllerName configuration",
              severity: InfoBarSeverity.error
          );
          break;
        case ServerResultType.freeingPort:
        case ServerResultType.freePortSuccess:
        case ServerResultType.freePortError:
          showMessage(
              "Message",
              loading: event.type == ServerResultType.freeingPort,
              severity: event.type == ServerResultType.freeingPort ? InfoBarSeverity.info : event.type == ServerResultType.freePortSuccess ? InfoBarSeverity.success : InfoBarSeverity.error
          );
          break;
        case ServerResultType.pingingRemote:
          showMessage(
              "Pinging remote server...",
              severity: InfoBarSeverity.info,
              loading: true,
              duration: const Duration(seconds: 10)
          );
          break;
        case ServerResultType.pingingLocal:
          showMessage(
              "Pinging ${type().name} server...",
              severity: InfoBarSeverity.info,
              loading: true,
              duration: const Duration(seconds: 10)
          );
          break;
        case ServerResultType.pingError:
          showMessage(
              "Cannot ping ${type().name} server",
              severity: InfoBarSeverity.error
          );
          break;
        case ServerResultType.startSuccess:
          if(showSuccessMessage) {
            showMessage(
                "The $controllerName was started successfully",
                severity: InfoBarSeverity.success
            );
          }
          completer.complete(true);
          break;
        case ServerResultType.startError:
          showMessage(
              "An error occurred while starting the $controllerName: ${event.error ?? "unknown error"}",
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
      watchProcess(embeddedServer!.pid).then((value) {
        if(started()) {
          pageIndex.value = 3;
          started.value = false;
          WidgetsBinding.instance.addPostFrameCallback((_) => showMessage(
              "The $controllerName was terminated unexpectedly: if this wasn't intentional, file a bug report",
              severity: InfoBarSeverity.warning,
              duration: snackbarLongDuration
          ));
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
      _onSuccess(embedded, encryptedIp, author);
      return;
    }

    var confirmPassword = await _askForPassword();
    if(confirmPassword == null) {
      return;
    }

    if(!checkPassword(confirmPassword, hashedPassword)) {
      showMessage(
          "Wrong password: please try again",
          duration: snackbarLongDuration,
          severity: InfoBarSeverity.error
      );
      return;
    }

    var decryptedIp = aes256Decrypt(encryptedIp, confirmPassword);
    _onSuccess(embedded, decryptedIp, author);
  }


  Future<String?> _askForPassword() async {
    var confirmPasswordController = TextEditingController();
    var showPassword = RxBool(false);
    var showPasswordTrailing = RxBool(false);
    return await showDialog<String?>(
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
    WidgetsBinding.instance.addPostFrameCallback((_) => showMessage(
        embedded ? "You joined $author's server successfully!" : "Copied IP to the clipboard",
        duration: snackbarLongDuration,
        severity: InfoBarSeverity.success
    ));
  }
}