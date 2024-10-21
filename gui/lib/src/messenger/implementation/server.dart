import 'dart:async';

import 'package:clipboard/clipboard.dart';
import 'package:fluent_ui/fluent_ui.dart' hide FluentIcons;
import 'package:fluentui_system_icons/fluentui_system_icons.dart';
import 'package:flutter/foundation.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/backend_controller.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/messenger/abstract/dialog.dart';
import 'package:reboot_launcher/src/messenger/abstract/info_bar.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/page/pages.dart';
import 'package:reboot_launcher/src/util/cryptography.dart';
import 'package:reboot_launcher/src/util/matchmaker.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:url_launcher/url_launcher.dart';

final List<InfoBarEntry> _infoBars = [];

extension ServerControllerDialog on BackendController {
  void cancelInteractive() {
    worker?.cancel(); // Do not await or it will hang
    _infoBars.forEach((infoBar) => infoBar.close());
    _infoBars.clear();
  }

  Future<bool> toggleInteractive() async {
    cancelInteractive();
    final stream = toggle(
      onExit: () {
        cancelInteractive();
        _showRebootInfoBar(
            translations.backendProcessError,
            severity: InfoBarSeverity.error
        );
      },
      onError: (errorMessage) {
        cancelInteractive();
        _showRebootInfoBar(
            translations.backendErrorMessage,
            severity: InfoBarSeverity.error,
            duration: infoBarLongDuration,
            action: Button(
              onPressed: () => launchUrl(launcherLogFile.uri),
              child: Text(translations.openLog),
            )
        );
      }
    );
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
    log("[BACKEND] Handling event: $event");
    switch (event.type) {
      case ServerResultType.starting:
        return _showRebootInfoBar(
            translations.startingServer,
            severity: InfoBarSeverity.info,
            loading: true,
            duration: null
        );
      case ServerResultType.startSuccess:
        return _showRebootInfoBar(
            type.value == ServerType.local ? translations.checkedServer : translations.startedServer,
            severity: InfoBarSeverity.success
        );
      case ServerResultType.startError:
        print(event.stackTrace);
        return _showRebootInfoBar(
            type.value == ServerType.local ? translations.localServerError(event.error ?? translations.unknownError) : translations.startServerError(event.error ?? translations.unknownError),
            severity: InfoBarSeverity.error,
            duration: infoBarLongDuration
        );
      case ServerResultType.stopping:
        return _showRebootInfoBar(
            translations.stoppingServer,
            severity: InfoBarSeverity.info,
            loading: true,
            duration: null
        );
      case ServerResultType.stopSuccess:
        return _showRebootInfoBar(
            translations.stoppedServer,
            severity: InfoBarSeverity.success
        );
      case ServerResultType.stopError:
        return _showRebootInfoBar(
            translations.stopServerError(event.error ?? translations.unknownError),
            severity: InfoBarSeverity.error,
            duration: infoBarLongDuration
        );
      case ServerResultType.missingHostError:
        return _showRebootInfoBar(
            translations.missingHostNameError,
            severity: InfoBarSeverity.error
        );
      case ServerResultType.missingPortError:
        return _showRebootInfoBar(
            translations.missingPortError,
            severity: InfoBarSeverity.error
        );
      case ServerResultType.illegalPortError:
        return _showRebootInfoBar(
            translations.illegalPortError,
            severity: InfoBarSeverity.error
        );
      case ServerResultType.freeingPort:
        return _showRebootInfoBar(
            translations.freeingPort,
            loading: true,
            duration: null
        );
      case ServerResultType.freePortSuccess:
        return _showRebootInfoBar(
            translations.freedPort,
            severity: InfoBarSeverity.success,
            duration: infoBarShortDuration
        );
      case ServerResultType.freePortError:
        return _showRebootInfoBar(
            translations.freePortError(event.error ?? translations.unknownError),
            severity: InfoBarSeverity.error,
            duration: infoBarLongDuration
        );
      case ServerResultType.pingingRemote:
        return _showRebootInfoBar(
            translations.pingingServer(ServerType.remote.name),
            severity: InfoBarSeverity.info,
            loading: true,
            duration: null
        );
      case ServerResultType.pingingLocal:
        return _showRebootInfoBar(
            translations.pingingServer(type.value.name),
            severity: InfoBarSeverity.info,
            loading: true,
            duration: null
        );
      case ServerResultType.pingError:
        return _showRebootInfoBar(
            translations.pingError(type.value.name),
            severity: InfoBarSeverity.error
        );
    }
  }

  Future<void> joinServerInteractive(String uuid, FortniteServer server) async {
    if(!kDebugMode && uuid == server.id) {
      _showRebootInfoBar(
          translations.joinSelfServer,
          duration: infoBarLongDuration,
          severity: InfoBarSeverity.error
      );
      return;
    }

    final gameController = Get.find<GameController>();
    final version = gameController.getVersionByName(server.version.toString());
    if(version == null) {
      _showRebootInfoBar(
          translations.cannotJoinServerVersion(server.version.toString()),
          duration: infoBarLongDuration,
          severity: InfoBarSeverity.error
      );
      return;
    }

    final hashedPassword = server.password;
    final hasPassword = hashedPassword != null;
    final embedded = type.value == ServerType.embedded;
    final author = server.author;
    final encryptedIp = server.ip;
    if(!hasPassword) {
      final valid = await _isServerValid(encryptedIp);
      if(!valid) {
        return;
      }

      _onSuccess(gameController, embedded, encryptedIp, author, version);
      return;
    }

    final confirmPassword = await _askForPassword();
    if(confirmPassword == null) {
      return;
    }

    if(!checkPassword(confirmPassword, hashedPassword)) {
      _showRebootInfoBar(
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

    _onSuccess(gameController, embedded, decryptedIp, author, version);
  }

  Future<bool> _isServerValid(String address) async {
    final result = await pingGameServer(address);
    if(result) {
      return true;
    }

    _showRebootInfoBar(
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
    return await showRebootDialog<String?>(
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
                              shape: WidgetStateProperty.all(const CircleBorder()),
                              backgroundColor: WidgetStateProperty.all(Colors.transparent)
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

  void _onSuccess(GameController controller, bool embedded, String decryptedIp, String author, FortniteVersion version) {
    if(embedded) {
      gameServerAddress.text = decryptedIp;
      pageIndex.value = RebootPageType.play.index;
    }else {
      FlutterClipboard.controlC(decryptedIp);
    }
    controller.selectedVersion = version;
    WidgetsBinding.instance.addPostFrameCallback((_) => _showRebootInfoBar(
        embedded ? translations.joinedServer(author) : translations.copiedIp,
        duration: infoBarLongDuration,
        severity: InfoBarSeverity.success
    ));
  }

  InfoBarEntry _showRebootInfoBar(dynamic text, {
    InfoBarSeverity severity = InfoBarSeverity.info,
    bool loading = false,
    Duration? duration = infoBarShortDuration,
    void Function()? onDismissed,
    Widget? action
  }) {
    final result = showRebootInfoBar(
      text,
      severity: severity,
      loading: loading,
      duration: duration,
      onDismissed: onDismissed,
      action: action
    );
    if(severity == InfoBarSeverity.info || severity == InfoBarSeverity.success) {
      _infoBars.add(result);
    }
    return result;
  }
}