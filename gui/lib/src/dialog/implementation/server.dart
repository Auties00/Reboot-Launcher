import 'dart:async';

import 'package:clipboard/clipboard.dart';
import 'package:dart_ipify/dart_ipify.dart';
import 'package:fluent_ui/fluent_ui.dart' hide FluentIcons;
import 'package:fluentui_system_icons/fluentui_system_icons.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/backend_controller.dart';
import 'package:reboot_launcher/src/controller/hosting_controller.dart';
import 'package:reboot_launcher/src/dialog/abstract/dialog.dart';
import 'package:reboot_launcher/src/dialog/abstract/dialog_button.dart';
import 'package:reboot_launcher/src/dialog/abstract/info_bar.dart';
import 'package:reboot_launcher/src/page/pages.dart';
import 'package:reboot_launcher/src/util/cryptography.dart';
import 'package:reboot_launcher/src/util/matchmaker.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:supabase_flutter/supabase_flutter.dart';
import 'package:sync/semaphore.dart';

final Semaphore _publishingSemaphore = Semaphore();

extension ServerControllerDialog on BackendController {
  Future<bool> toggleInteractive() async {
    final stream = toggle();
    final completer = Completer<bool>();
    InfoBarEntry? entry;
    worker = stream.listen((event) {
      entry?.close();
      entry = _handeEvent(event);
      if(event.type.isError) {
        completer.complete(false);
      }else if(event.type.isSuccess) {
        completer.complete(true);
      }
    });

    return await completer.future;
  }

  InfoBarEntry _handeEvent(ServerResult event) {
     switch (event.type) {
      case ServerResultType.starting:
        return showInfoBar(
            translations.startingServer,
            severity: InfoBarSeverity.info,
            loading: true,
            duration: null
        );
      case ServerResultType.startSuccess:
        return showInfoBar(
            type.value == ServerType.local ? translations.checkedServer : translations.startedServer,
            severity: InfoBarSeverity.success
        );
      case ServerResultType.startError:
        return showInfoBar(
            type.value == ServerType.local ? translations.localServerError(event.error ?? translations.unknownError) : translations.startServerError(event.error ?? translations.unknownError),
            severity: InfoBarSeverity.error,
            duration: infoBarLongDuration
        );
      case ServerResultType.stopping:
        return showInfoBar(
            translations.stoppingServer,
            severity: InfoBarSeverity.info,
            loading: true,
            duration: null
        );
      case ServerResultType.stopSuccess:
        return showInfoBar(
            translations.stoppedServer,
            severity: InfoBarSeverity.success
        );
      case ServerResultType.stopError:
        return showInfoBar(
            translations.stopServerError(event.error ?? translations.unknownError),
            severity: InfoBarSeverity.error,
            duration: infoBarLongDuration
        );
      case ServerResultType.missingHostError:
        return showInfoBar(
            translations.missingHostNameError,
            severity: InfoBarSeverity.error
        );
      case ServerResultType.missingPortError:
        return showInfoBar(
            translations.missingPortError,
            severity: InfoBarSeverity.error
        );
      case ServerResultType.illegalPortError:
        return showInfoBar(
            translations.illegalPortError,
            severity: InfoBarSeverity.error
        );
      case ServerResultType.freeingPort:
        return showInfoBar(
            translations.freeingPort,
            loading: true,
            duration: null
        );
      case ServerResultType.freePortSuccess:
        return showInfoBar(
            translations.freedPort,
            severity: InfoBarSeverity.success,
            duration: infoBarShortDuration
        );
      case ServerResultType.freePortError:
        return showInfoBar(
            translations.freePortError(event.error ?? translations.unknownError),
            severity: InfoBarSeverity.error,
            duration: infoBarLongDuration
        );
      case ServerResultType.pingingRemote:
        return showInfoBar(
            translations.pingingRemoteServer,
            severity: InfoBarSeverity.info,
            loading: true,
            duration: null
        );
      case ServerResultType.pingingLocal:
        return showInfoBar(
            translations.pingingLocalServer(type.value.name),
            severity: InfoBarSeverity.info,
            loading: true,
            duration: null
        );
      case ServerResultType.pingError:
        return showInfoBar(
            translations.pingError(type.value.name),
            severity: InfoBarSeverity.error
        );
    }
  }

  void joinLocalHost() {
    gameServerAddress.text = kDefaultGameServerHost;
    gameServerOwner.value = null;
  }

  Future<void> joinServer(String uuid, Map<String, dynamic> entry) async {
    final id = entry["id"];
    if(uuid == id) {
      showInfoBar(
          translations.joinSelfServer,
          duration: infoBarLongDuration,
          severity: InfoBarSeverity.error
      );
      return;
    }

    final hashedPassword = entry["password"];
    final hasPassword = hashedPassword != null;
    final embedded = type.value == ServerType.embedded;
    final author = entry["author"];
    final encryptedIp = entry["ip"];
    if(!hasPassword) {
      final valid = await _isServerValid(encryptedIp);
      if(!valid) {
        return;
      }

      _onSuccess(embedded, encryptedIp, author);
      return;
    }

    final confirmPassword = await _askForPassword();
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

    final decryptedIp = aes256Decrypt(encryptedIp, confirmPassword);
    final valid = await _isServerValid(decryptedIp);
    if(!valid) {
      return;
    }

    _onSuccess(embedded, decryptedIp, author);
  }

  Future<bool> _isServerValid(String address) async {
    final result = await pingGameServer(address);
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

      final supabase = Supabase.instance.client;
      final hosts = supabase.from("hosting");
      final payload = {
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