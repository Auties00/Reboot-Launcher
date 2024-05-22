import 'dart:async';

import 'package:clipboard/clipboard.dart';
import 'package:dart_ipify/dart_ipify.dart';
import 'package:fluent_ui/fluent_ui.dart' hide FluentIcons;
import 'package:fluentui_system_icons/fluentui_system_icons.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/hosting_controller.dart';
import 'package:reboot_launcher/src/controller/matchmaker_controller.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';
import 'package:reboot_launcher/src/dialog/abstract/dialog.dart';
import 'package:reboot_launcher/src/dialog/abstract/dialog_button.dart';
import 'package:reboot_launcher/src/dialog/abstract/info_bar.dart';
import 'package:reboot_launcher/src/page/pages.dart';
import 'package:reboot_launcher/src/util/cryptography.dart';
import 'package:reboot_launcher/src/util/matchmaker.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:supabase_flutter/supabase_flutter.dart';
import 'package:sync/semaphore.dart';

extension ServerControllerDialog on ServerController {
  Future<bool> toggleInteractive([bool showSuccessMessage = true]) async {
    var stream = toggle();
    var completer = Completer<bool>();
    worker = stream.listen((event) {
      print(event.type);
      switch (event.type) {
        case ServerResultType.starting:
          showInfoBar(
              translations.startingServer(controllerName),
              severity: InfoBarSeverity.info,
              loading: true,
              duration: null
          );
          break;
        case ServerResultType.startSuccess:
          if(showSuccessMessage) {
            showInfoBar(
                type.value == ServerType.local ? translations.checkedServer(controllerName) : translations.startedServer(controllerName),
                severity: InfoBarSeverity.success
            );
          }
          completer.complete(true);
          break;
        case ServerResultType.startError:
          showInfoBar(
              type.value == ServerType.local ? translations.localServerError(event.error ?? translations.unknownError, controllerName) : translations.startServerError(event.error ?? translations.unknownError, controllerName),
              severity: InfoBarSeverity.error,
              duration: infoBarLongDuration
          );
          break;
        case ServerResultType.stopping:
          showInfoBar(
              translations.stoppingServer,
              severity: InfoBarSeverity.info,
              loading: true,
              duration: null
          );
          break;
        case ServerResultType.stopSuccess:
          if(showSuccessMessage) {
            showInfoBar(
                translations.stoppedServer(controllerName),
                severity: InfoBarSeverity.success
            );
          }
          completer.complete(true);
          break;
        case ServerResultType.stopError:
          showInfoBar(
              translations.stopServerError(
                  event.error ?? translations.unknownError, controllerName),
              severity: InfoBarSeverity.error,
              duration: infoBarLongDuration
          );
          break;
        case ServerResultType.missingHostError:
          showInfoBar(
              translations.missingHostNameError(controllerName),
              severity: InfoBarSeverity.error
          );
          break;
        case ServerResultType.missingPortError:
          showInfoBar(
              translations.missingPortError(controllerName),
              severity: InfoBarSeverity.error
          );
          break;
        case ServerResultType.illegalPortError:
          showInfoBar(
              translations.illegalPortError(controllerName),
              severity: InfoBarSeverity.error
          );
          break;
        case ServerResultType.freeingPort:
          showInfoBar(
              translations.freeingPort(defaultPort),
              loading: true,
              duration: null
          );
          break;
        case ServerResultType.freePortSuccess:
          showInfoBar(
              translations.freedPort(defaultPort),
              severity: InfoBarSeverity.success,
              duration: infoBarShortDuration
          );
          break;
        case ServerResultType.freePortError:
          showInfoBar(
              translations.freePortError(event.error ?? translations.unknownError, controllerName),
              severity: InfoBarSeverity.error,
              duration: infoBarLongDuration
          );
          break;
        case ServerResultType.pingingRemote:
          if(started.value) {
            showInfoBar(
                translations.pingingRemoteServer(controllerName),
                severity: InfoBarSeverity.info,
                loading: true,
                duration: null
            );
          }
          break;
        case ServerResultType.pingingLocal:
          showInfoBar(
              translations.pingingLocalServer(controllerName, type().name),
              severity: InfoBarSeverity.info,
              loading: true,
              duration: null
          );
          break;
        case ServerResultType.pingError:
          showInfoBar(
              translations.pingError(controllerName, type().name),
              severity: InfoBarSeverity.error
          );
          break;
      }

      if(event.type.isError) {
        completer.complete(false);
      }
    });

    return await completer.future;
  }
}

final Semaphore _publishingSemaphore = Semaphore();

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
          duration: infoBarLongDuration,
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
          duration: infoBarLongDuration,
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
        duration: infoBarLongDuration,
        severity: InfoBarSeverity.error
    );
    return false;
  }

  Future<String?> _askForPassword() async {
    final confirmPasswordController = TextEditingController();
    final showPassword = RxBool(false);
    final showPasswordTrailing = RxBool(false);
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
                        suffix: !showPasswordTrailing.value ? null : Button(
                          onPressed: () => showPassword.value = !showPassword.value,
                          style: ButtonStyle(
                              shape: ButtonState.all(const CircleBorder()),
                              backgroundColor: ButtonState.all(Colors.transparent)
                          ),
                          child: Icon(
                              showPassword.value ? FluentIcons.eye_off_24_regular : FluentIcons.eye_24_regular
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
        duration: infoBarLongDuration,
        severity: InfoBarSeverity.success
    ));
  }
}

extension HostingControllerExtension on HostingController {
  Future<void> publishServer(String author, String version) async {
    try {
      _publishingSemaphore.acquire();
      if(published.value) {
        return;
      }

      final passwordText = password.text;
      final hasPassword = passwordText.isNotEmpty;
      var ip = await Ipify.ipv4();
      if(hasPassword) {
        ip = aes256Encrypt(ip, passwordText);
      }

      var supabase = Supabase.instance.client;
      var hosts = supabase.from("hosting");
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
    }catch(error) {
      published.value = false;
    }finally {
      _publishingSemaphore.release();
    }
  }

  Future<void> discardServer() async {
    try {
      _publishingSemaphore.acquire();
      if(!published.value) {
        return;
      }

      final supabase = Supabase.instance.client;
      await supabase.from("hosting")
          .delete()
          .match({'id': uuid});
      published.value = false;
    }catch(_) {
      published.value = true;
    }finally {
      _publishingSemaphore.release();
    }
  }
}