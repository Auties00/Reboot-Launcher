import 'dart:async';
import 'dart:io';

import 'package:clipboard/clipboard.dart';
import 'package:fluent_ui/fluent_ui.dart' hide FluentIcons;
import 'package:fluentui_system_icons/fluentui_system_icons.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/main.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/messenger/dialog.dart';
import 'package:reboot_launcher/src/messenger/info_bar.dart';
import 'package:reboot_launcher/src/page/page_type.dart';
import 'package:reboot_launcher/src/page/pages.dart';
import 'package:reboot_launcher/src/util/cryptography.dart';
import 'package:reboot_launcher/src/util/keyboard.dart';
import 'package:reboot_launcher/src/util/matchmaker.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:url_launcher/url_launcher.dart';

class BackendController extends GetxController {
  static const String storageName = "v2_backend_storage";
  static const PhysicalKeyboardKey _kDefaultConsoleKey = PhysicalKeyboardKey(0x00070041);

  late final GetStorage? _storage;
  late final TextEditingController host;
  late final TextEditingController port;
  late final Rx<ServerType> type;
  late final TextEditingController gameServerAddress;
  late final FocusNode gameServerAddressFocusNode;
  late final Rx<PhysicalKeyboardKey> consoleKey;
  late final RxBool started;
  late final RxBool detached;
  late final List<InfoBarEntry> _infoBars;
  StreamSubscription? worker;
  int? embeddedProcessPid;
  HttpServer? localServer;
  HttpServer? remoteServer;

  BackendController() {
    _storage = appWithNoStorage ? null : GetStorage(storageName);
    started = RxBool(false);
    type = Rx(ServerType.values.elementAt(_storage?.read("type") ?? 0));
    type.listen((value) {
      host.text = _readHost();
      port.text = _readPort();
      _storage?.write("type", value.index);
      if (!started.value) {
        return;
      }

      stop();
    });
    host = TextEditingController(text: _readHost());
    host.addListener(() =>
        _storage?.write("${type.value.name}_host", host.text));
    port = TextEditingController(text: _readPort());
    port.addListener(() =>
        _storage?.write("${type.value.name}_port", port.text));
    detached = RxBool(_storage?.read("detached") ?? false);
    detached.listen((value) => _storage?.write("detached", value));
    final address = _storage?.read("game_server_address");
    gameServerAddress = TextEditingController(text: address == null || address.isEmpty ? "127.0.0.1" : address);
    var lastValue = gameServerAddress.text;
    writeMatchmakingIp(lastValue);
    gameServerAddress.addListener(() {
      var newValue = gameServerAddress.text;
      if(newValue.trim().toLowerCase() == lastValue.trim().toLowerCase()) {
        return;
      }

      lastValue = newValue;
      gameServerAddress.selection = TextSelection.collapsed(offset: newValue.length);
      _storage?.write("game_server_address", newValue);
      writeMatchmakingIp(newValue);
    });
    watchMatchmakingIp().listen((event) {
      if(event != null && gameServerAddress.text != event) {
        gameServerAddress.text = event;
      }
    });
    gameServerAddressFocusNode = FocusNode();
    consoleKey = Rx(() {
      final consoleKeyValue = _storage?.read("console_key");
      if(consoleKeyValue == null) {
        return _kDefaultConsoleKey;
      }

      final consoleKeyNumber = int.tryParse(consoleKeyValue.toString());
      if(consoleKeyNumber == null) {
        return _kDefaultConsoleKey;
      }

      final consoleKey = PhysicalKeyboardKey(consoleKeyNumber);
      if(!consoleKey.isUnrealEngineKey) {
        return _kDefaultConsoleKey;
      }

      return consoleKey;
    }());
    _writeConsoleKey(consoleKey.value);
    consoleKey.listen((newValue) {
      _storage?.write("console_key", newValue.usbHidUsage);
      _writeConsoleKey(newValue);
    });
    _infoBars = [];
  }

  Future<void> _writeConsoleKey(PhysicalKeyboardKey keyValue) async {
    final defaultInput = File("${backendDirectory.path}\\CloudStorage\\DefaultInput.ini");
    await defaultInput.parent.create(recursive: true);
    await defaultInput.writeAsString("[/Script/Engine.InputSettings]\n+ConsoleKeys=Tilde\n+ConsoleKeys=${keyValue.unrealEngineName}", flush: true);
  }

  String _readHost() {
    String? value = _storage?.read("${type.value.name}_host");
    if (value != null && value.isNotEmpty) {
      return value;
    }

    if (type.value != ServerType.remote) {
      return kDefaultBackendHost;
    }

    return "";
  }

  String _readPort() => _storage?.read("${type.value.name}_port") ?? kDefaultBackendPort.toString();

  void joinLocalhost() {
    gameServerAddress.text = kDefaultGameServerHost;
  }

  void reset() async {
    type.value = ServerType.values.elementAt(0);
    for (final type in ServerType.values) {
      _storage?.write("${type.name}_host", null);
      _storage?.write("${type.name}_port", null);
    }

    host.text = type.value != ServerType.remote ? kDefaultBackendHost : "";
    port.text = kDefaultBackendPort.toString();
    gameServerAddress.text = "127.0.0.1";
    consoleKey.value = _kDefaultConsoleKey;
    detached.value = false;
  }

  Future<bool> toggleInteractive() async {
    _cancel();
    final stream = started.value ? stop() : start(
        onExit: () {
          _cancel();
          _showRebootInfoBar(
              translations.backendProcessError,
              severity: InfoBarSeverity.error
          );
        },
        onError: (errorMessage) {
          _cancel();
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

  Stream<ServerResult> start({required void Function() onExit, required void Function(String) onError}) async* {
    try {
      if(started.value) {
        return;
      }

      final serverType = type.value;
      final hostData = this.host.text.trim();
      final portData = this.port.text.trim();
      started.value = true;
      if(serverType != ServerType.local || portData != kDefaultBackendPort.toString()) {
        yield ServerResult(ServerResultType.starting);
      }

      if (hostData.isEmpty) {
        yield ServerResult(ServerResultType.missingHostError);
        started.value = false;
        return;
      }

      if (portData.isEmpty) {
        yield ServerResult(ServerResultType.missingPortError);
        started.value = false;
        return;
      }

      final portNumber = int.tryParse(portData);
      if (portNumber == null) {
        yield ServerResult(ServerResultType.illegalPortError);
        started.value = false;
        return;
      }

      if ((serverType != ServerType.local || portData != kDefaultBackendPort.toString()) && !(await isBackendPortFree())) {
        yield ServerResult(ServerResultType.freeingPort);
        final result = await freeBackendPort();
        yield ServerResult(result ? ServerResultType.freePortSuccess : ServerResultType.freePortError);
        if(!result) {
          started.value = false;
          return;
        }
      }

      switch(serverType){
        case ServerType.embedded:
          final process = await startEmbeddedBackend(detached.value, onError: (errorMessage) {
            if(started.value) {
              started.value = false;
              onError(errorMessage);
            }
          });
          watchProcess(process.pid).then((_) {
            if(started.value) {
              started.value = false;
              onExit();
            }
          });
          embeddedProcessPid = process.pid;
          break;
        case ServerType.remote:
          yield ServerResult(ServerResultType.pingingRemote);
          final uriResult = await pingBackend(hostData, portNumber);
          if(uriResult == null) {
            yield ServerResult(ServerResultType.pingError);
            started.value = false;
            return;
          }

          remoteServer = await startRemoteBackendProxy(uriResult);
          break;
        case ServerType.local:
          if(portNumber != kDefaultBackendPort) {
            yield ServerResult(ServerResultType.pingingLocal);
            final uriResult = await pingBackend(kDefaultBackendHost, portNumber);
            if(uriResult == null) {
              yield ServerResult(ServerResultType.pingError);
              started.value = false;
              return;
            }

            localServer = await startRemoteBackendProxy(Uri.parse("http://$kDefaultBackendHost:$portData"));
          }else {
            // If the local server is running on port 3551 there is no reverse proxy running
            // We only need to check if everything is working
            started.value = false;
          }

          break;
      }

      yield ServerResult(ServerResultType.pingingLocal);
      final uriResult = await pingBackend(kDefaultBackendHost, kDefaultBackendPort);
      if(uriResult == null) {
        yield ServerResult(ServerResultType.pingError);
        remoteServer?.close(force: true);
        localServer?.close(force: true);
        started.value = false;
        return;
      }

      yield ServerResult(ServerResultType.startSuccess);
    }catch(error, stackTrace) {
      yield ServerResult(
          ServerResultType.startError,
          error: error,
          stackTrace: stackTrace
      );
      remoteServer?.close(force: true);
      localServer?.close(force: true);
      started.value = false;
    }
  }

  Stream<ServerResult> stop() async* {
    if(!started.value) {
      return;
    }

    yield ServerResult(ServerResultType.stopping);
    started.value = false;
    try{
      switch(type()){
        case ServerType.embedded:
          final embeddedProcessPid = this.embeddedProcessPid;
          if(embeddedProcessPid != null) {
            Process.killPid(embeddedProcessPid, ProcessSignal.sigterm);
            this.embeddedProcessPid = null;
          }
          break;
        case ServerType.remote:
          await remoteServer?.close(force: true);
          remoteServer = null;
          break;
        case ServerType.local:
          await localServer?.close(force: true);
          localServer = null;
          break;
      }
      yield ServerResult(ServerResultType.stopSuccess);
    }catch(error, stackTrace){
      yield ServerResult(
          ServerResultType.stopError,
          error: error,
          stackTrace: stackTrace
      );
      started.value = true;
    }
  }

  void _cancel() {
    worker?.cancel(); // Do not await or it will hang
    _infoBars.forEach((infoBar) => infoBar.close());
    _infoBars.clear();
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

  Future<void> joinServer(String uuid, FortniteServer server) async {
    if(!kDebugMode && uuid == server.id) {
      _showRebootInfoBar(
          translations.joinSelfServer,
          duration: infoBarLongDuration,
          severity: InfoBarSeverity.error
      );
      return;
    }

    final version = Get.find<GameController>()
        .getVersionByName(server.version.toString());
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

      _onServerJoined(embedded, encryptedIp, author, version);
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

    _onServerJoined(embedded, decryptedIp, author, version);
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

  void _onServerJoined(bool embedded, String decryptedIp, String author, FortniteVersion version) {
    if(embedded) {
      gameServerAddress.text = decryptedIp;
      pageIndex.value = RebootPageType.play.index;
    }else {
      FlutterClipboard.controlC(decryptedIp);
    }
    Get.find<GameController>()
        .selectedVersion = version;
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