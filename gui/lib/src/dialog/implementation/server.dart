import 'dart:async';

import 'package:clipboard/clipboard.dart';
import 'package:dart_ipify/dart_ipify.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/material.dart' show Icons;
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/hosting_controller.dart';
import 'package:reboot_launcher/src/controller/matchmaker_controller.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';
import 'package:reboot_launcher/src/dialog/abstract/dialog.dart';
import 'package:reboot_launcher/src/dialog/abstract/dialog_button.dart';
import 'package:reboot_launcher/src/dialog/abstract/info_bar.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/page/pages.dart';
import 'package:reboot_launcher/src/util/cryptography.dart';
import 'package:reboot_launcher/src/util/matchmaker.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:supabase_flutter/supabase_flutter.dart';

extension ServerControllerDialog on ServerController {
  Future<bool> toggleInteractive(RebootPageType caller, [bool showSuccessMessage = true]) async {
    var stream = toggle();
    return await _handleStream(caller, stream, showSuccessMessage);
  }

  Future<bool> _handleStream(RebootPageType caller, Stream<ServerResult> stream, bool showSuccessMessage) async {
    var completer = Completer<bool>();
    worker = stream.listen((event) {
      switch (event.type) {
        case ServerResultType.starting:
          showInfoBar(
              translations.startingServer(controllerName),
              pageType: caller,
              severity: InfoBarSeverity.info,
              loading: true,
              duration: null
          );
          break;
        case ServerResultType.startSuccess:
          if(showSuccessMessage) {
            showInfoBar(
                translations.startedServer(controllerName),
                pageType: caller,
                severity: InfoBarSeverity.success
            );
          }
          completer.complete(true);
          break;
        case ServerResultType.startError:
          showInfoBar(
              translations.startServerError(
                  event.error ?? translations.unknownError, controllerName),
              pageType: caller,
              severity: InfoBarSeverity.error,
              duration: snackbarLongDuration
          );
          break;
        case ServerResultType.stopping:
          showInfoBar(
              translations.stoppingServer,
              pageType: caller,
              severity: InfoBarSeverity.info,
              loading: true,
              duration: null
          );
          break;
        case ServerResultType.stopSuccess:
          if(showSuccessMessage) {
            showInfoBar(
                translations.stoppedServer(controllerName),
                pageType: caller,
                severity: InfoBarSeverity.success
            );
          }
          completer.complete(true);
          break;
        case ServerResultType.stopError:
          showInfoBar(
              translations.stopServerError(
                  event.error ?? translations.unknownError, controllerName),
              pageType: caller,
              severity: InfoBarSeverity.error,
              duration: snackbarLongDuration
          );
          break;
        case ServerResultType.missingHostError:
          showInfoBar(
              translations.missingHostNameError(controllerName),
              pageType: caller,
              severity: InfoBarSeverity.error
          );
          break;
        case ServerResultType.missingPortError:
          showInfoBar(
              translations.missingPortError(controllerName),
              pageType: caller,
              severity: InfoBarSeverity.error
          );
          break;
        case ServerResultType.illegalPortError:
          showInfoBar(
              translations.illegalPortError(controllerName),
              pageType: caller,
              severity: InfoBarSeverity.error
          );
          break;
        case ServerResultType.freeingPort:
          showInfoBar(
              translations.freeingPort(defaultPort),
              pageType: caller,
              loading: true,
              duration: null
          );
          break;
        case ServerResultType.freePortSuccess:
          showInfoBar(
              translations.freedPort(defaultPort),
              pageType: caller,
              severity: InfoBarSeverity.success,
              duration: snackbarShortDuration
          );
          break;
        case ServerResultType.freePortError:
          showInfoBar(
              translations.freePortError(event.error ?? translations.unknownError, controllerName),
              pageType: caller,
              severity: InfoBarSeverity.error,
              duration: snackbarLongDuration
          );
          break;
        case ServerResultType.pingingRemote:
          if(started.value) {
            showInfoBar(
                translations.pingingRemoteServer(controllerName),
                pageType: caller,
                severity: InfoBarSeverity.info,
                loading: true,
                duration: null
            );
          }
          break;
        case ServerResultType.pingingLocal:
          if(started.value) {
            showInfoBar(
                translations.pingingLocalServer(controllerName, type().name),
                pageType: caller,
                severity: InfoBarSeverity.info,
                loading: true,
                duration: null
            );
          }
          break;
        case ServerResultType.pingError:
          showInfoBar(
              translations.pingError(controllerName, type().name),
              pageType: caller,
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
  void joinLocalHost() {
    gameServerAddress.text = kDefaultGameServerHost;
    gameServerOwner.value = null;
  }

  Future<void> joinServer(String uuid, Map<String, dynamic> entry) async {
    var id = entry["id"];
    if(uuid == id) {
      showInfoBar(
          translations.joinSelfServer,
          duration: snackbarLongDuration,
          severity: InfoBarSeverity.error
      );
      return;
    }

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
          translations.wrongServerPassword,
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
        translations.offlineServer,
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
                    label: translations.serverPassword,
                    child: Obx(() => TextFormBox(
                        placeholder: translations.serverPasswordPlaceholder,
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
                  text: translations.serverPasswordCancel,
                  type: ButtonType.secondary
              ),

              DialogButton(
                  text: translations.serverPasswordConfirm,
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
      gameServerOwner.value = author;
      pageIndex.value = 0;
    }else {
      FlutterClipboard.controlC(decryptedIp);
    }
    WidgetsBinding.instance.addPostFrameCallback((_) => showInfoBar(
        embedded ? translations.joinedServer(author) : translations.copiedIp,
        duration: snackbarLongDuration,
        severity: InfoBarSeverity.success
    ));
  }
}

extension HostingControllerExtension on HostingController {
  Future<void> publishServer(String author, String version) async {
    var passwordText = password.text;
    var hasPassword = passwordText.isNotEmpty;
    var ip = await Ipify.ipv4();
    if(hasPassword) {
      ip = aes256Encrypt(ip, passwordText);
    }

    var supabase = Supabase.instance.client;
    var hosts = supabase.from('hosts');
    var payload = {
      'name': name.text,
      'description': description.text,
      'author': author,
      'ip': ip,
      'version': version,
      'password': hasPassword ? hashPassword(passwordText) : null,
      'timestamp': DateTime.now().toIso8601String(),
      'discoverable': discoverable.value
    };
    if(published()) {
      await hosts.update(payload).eq("id", uuid);
    }else {
      payload["id"] = uuid;
      await hosts.insert(payload);
    }

    published.value = true;
  }

  Future<void> discardServer() async {
    var supabase = Supabase.instance.client;
    await supabase.from('hosts')
        .delete()
        .match({'id': uuid});
    published.value = false;
  }
}