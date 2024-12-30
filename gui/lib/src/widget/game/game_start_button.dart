import 'dart:async';
import 'dart:io';

import 'package:async/async.dart';
import 'package:dart_ipify/dart_ipify.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:local_notifier/local_notifier.dart';
import 'package:path/path.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/backend_controller.dart';
import 'package:reboot_launcher/src/controller/dll_controller.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/hosting_controller.dart';
import 'package:reboot_launcher/src/messenger/dialog.dart';
import 'package:reboot_launcher/src/messenger/info_bar.dart';
import 'package:reboot_launcher/src/page/pages.dart';
import 'package:reboot_launcher/src/util/matchmaker.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:url_launcher/url_launcher.dart';
import 'package:url_launcher/url_launcher_string.dart';
import 'package:version/version.dart';

class LaunchButton extends StatefulWidget {
  final bool host;
  final String startLabel;
  final String stopLabel;

  const LaunchButton({Key? key, required this.host, required this.startLabel, required this.stopLabel}) : super(key: key);

  @override
  State<LaunchButton> createState() => _LaunchButtonState();
}

class _LaunchButtonState extends State<LaunchButton> {
  static const Duration _kRebootDelay = Duration(seconds: 10);

  final GameController _gameController = Get.find<GameController>();
  final HostingController _hostingController = Get.find<HostingController>();
  final BackendController _backendController = Get.find<BackendController>();
  final DllController _dllController = Get.find<DllController>();

  InfoBarEntry? _gameClientInfoBar;
  InfoBarEntry? _gameServerInfoBar;
  CancelableOperation? _operation;
  Completer? _pingOperation;
  IVirtualDesktop? _virtualDesktop;

  @override
  Widget build(BuildContext context) => Align(
    alignment: AlignmentDirectional.bottomCenter,
    child: SizedBox(
      width: double.infinity,
      child: Obx(() => SizedBox(
        height: 48,
        child: Button(
            onPressed: () => _operation = CancelableOperation.fromFuture(_toggle()),
            child: Align(
                alignment: Alignment.center,
                child: Text((widget.host ? _hostingController.started() : _gameController.started()) ? widget.stopLabel : widget.startLabel)
            )
        ),
      )),
    ),
  );

  void _setStarted(bool hosting, bool started) => hosting ? _hostingController.started.value = started : _gameController.started.value = started;

  Future<void> _toggle({bool? host}) async {
    host ??= widget.host;
    log("[${host ? 'HOST' : 'GAME'}] Toggling state");
    if (host ? _hostingController.started() : _gameController.started()) {
      log("[${host ? 'HOST' : 'GAME'}] User asked to close the current instance");
      _onStop(
          reason: _StopReason.normal
      );
      return;
    }

    final version = _gameController.selectedVersion;
    log("[${host ? 'HOST' : 'GAME'}] Version data: $version");
    if(version == null){
      log("[${host ? 'HOST' : 'GAME'}] No version selected");
      _onStop(
          reason: _StopReason.missingVersionError
      );
      return;
    }

    log("[${host ? 'HOST' : 'GAME'}] Setting started...");
    _setStarted(host, true);
    log("[${host ? 'HOST' : 'GAME'}] Set started");
    log("[${host ? 'HOST' : 'GAME'}] Checking dlls: ${InjectableDll.values}");
    for (final injectable in InjectableDll.values) {
      if(await _getDllFileOrStop(version.content, injectable, host) == null) {
        return;
      }
    }

    try {
      final executable = await version.shippingExecutable;
      if(executable == null){
        log("[${host ? 'HOST' : 'GAME'}] No executable found");
        _onStop(
            reason: _StopReason.missingExecutableError,
            error: version.location.path
        );
        return;
      }

      log("[${host ? 'HOST' : 'GAME'}] Checking backend(port: ${_backendController.type.value.name}, type: ${_backendController.type.value.name})...");
      final backendResult = _backendController.started() || await _backendController.toggleInteractive();
      if(!backendResult){
        log("[${host ? 'HOST' : 'GAME'}] Cannot start backend");
        _onStop(
            reason: _StopReason.backendError
        );
        return;
      }
      log("[${host ? 'HOST' : 'GAME'}] Backend works");
      final serverType = _hostingController.type.value;
      log("[${host ? 'HOST' : 'GAME'}] Implicit game server metadata: headless($serverType)");
      final linkedHostingInstance = await _startMatchMakingServer(version, host, serverType, false);
      log("[${host ? 'HOST' : 'GAME'}] Implicit game server result: $linkedHostingInstance");
      final result = await _startGameProcesses(version, host, serverType, linkedHostingInstance);
      final started = host ? _hostingController.started() : _gameController.started();
      if(!started) {
        result?.kill();
        return;
      }

      if(!host) {
        _showLaunchingGameClientWidget(version, serverType, linkedHostingInstance != null);
      }else {
        _showLaunchingGameServerWidget();
      }
    } on ProcessException catch (exception, stackTrace) {
      _onStop(
          reason: _StopReason.corruptedVersionError,
          error: exception.toString(),
          stackTrace: stackTrace
      );
    } catch (exception, stackTrace) {
      _onStop(
          reason: _StopReason.unknownError,
          error: exception.toString(),
          stackTrace: stackTrace
      );
    }
  }

  Future<GameInstance?> _startMatchMakingServer(FortniteVersion version, bool host, GameServerType hostType, bool forceLinkedHosting) async {
    log("[${host ? 'HOST' : 'GAME'}] Checking if a server needs to be started automatically...");
    if(host){
      log("[${host ? 'HOST' : 'GAME'}] The user clicked on Start hosting, so it's not necessary");
      return null;
    }

    if(!forceLinkedHosting && _backendController.type.value == ServerType.embedded && !isLocalHost(_backendController.gameServerAddress.text)) {
      log("[${host ? 'HOST' : 'GAME'}] Backend is not set to embedded and/or not pointing to the local game server");
      return null;
    }

    if(_hostingController.started()){
      log("[${host ? 'HOST' : 'GAME'}] The user has already manually started the hosting server");
      return null;
    }

    final response = forceLinkedHosting || await _askForAutomaticGameServer(host);
    if(!response) {
      log("[${host ? 'HOST' : 'GAME'}] The user disabled the automatic server");
      return null;
    }

    log("[${host ? 'HOST' : 'GAME'}] Starting implicit game server...");
    final instance = await _startGameProcesses(version, true, hostType, null);
    log("[${host ? 'HOST' : 'GAME'}] Started implicit game server...");
    _setStarted(true, true);
    log("[${host ? 'HOST' : 'GAME'}] Set implicit game server as started");
    return instance;
  }

  Future<bool> _askForAutomaticGameServer(bool host) async {
    if (host ? !_hostingController.started() : !_gameController.started()) {
      log("[${host ? 'HOST' : 'GAME'}] User asked to close the current instance");
      _onStop(reason: _StopReason.normal);
      return false;
    }

    final result = await showRebootDialog<bool>(
        builder: (context) => InfoDialog(
          text: translations.automaticGameServerDialogContent,
          buttons: [
            DialogButton(
                type: ButtonType.secondary,
                text: translations.automaticGameServerDialogIgnore
            ),
            DialogButton(
              type: ButtonType.primary,
              text: translations.automaticGameServerDialogStart,
              onTap: () => Navigator.of(context).pop(true),
            ),
          ],
        )
    ) ?? false;
    await Future.delayed(const Duration(seconds: 1));
    return result;
  }

  Future<GameInstance?> _startGameProcesses(FortniteVersion version, bool host, GameServerType hostType, GameInstance? linkedHosting) async {
    log("[${host ? 'HOST' : 'GAME'}] Starting game process...");
    log("[${host ? 'HOST' : 'GAME'}] Starting paused launcher...");
    final launcherProcess = await _createPausedProcess(version, version.launcherExecutable);

    log("[${host ? 'HOST' : 'GAME'}] Started paused launcher: $launcherProcess");
    log("[${host ? 'HOST' : 'GAME'}] Starting paused eac...");
    final eacProcess = await _createPausedProcess(version, version.eacExecutable);

    log("[${host ? 'HOST' : 'GAME'}] Started paused eac: $eacProcess");
    final executable = await version.shippingExecutable;
    log("[${host ? 'HOST' : 'GAME'}] Using game path: ${executable?.path}");
    final gameProcess = await _createGameProcess(version, executable!, host, hostType, linkedHosting);
    if(gameProcess == null) {
      log("[${host ? 'HOST' : 'GAME'}] No game process was created");
      return null;
    }

    log("[${host ? 'HOST' : 'GAME'}] Created game process: ${gameProcess}");
    final instance = GameInstance(
        version: version.content,
        gamePid: gameProcess,
        launcherPid: launcherProcess,
        eacPid: eacProcess,
        serverType: host ? hostType : null,
        child: linkedHosting
    );
    if(host){
      _hostingController.discardServer();
      _hostingController.instance.value = instance;
    }else{
      _gameController.instance.value = instance;
    }
    await _injectOrShowError(InjectableDll.auth, host);
    log("[${host ? 'HOST' : 'GAME'}] Finished creating game instance");
    return instance;
  }

  Future<int?> _createGameProcess(FortniteVersion version, File executable, bool host, GameServerType hostType, GameInstance? linkedHosting) async {
    log("[${host ? 'HOST' : 'GAME'}] Generating instance args...");
    final gameArgs = createRebootArgs(
        host ? _hostingController.accountUsername.text : _gameController.username.text,
        host ? _hostingController.accountPassword.text :_gameController.password.text,
        host,
        hostType,
        false,
        host ? _hostingController.customLaunchArgs.text : _gameController.customLaunchArgs.text
    );
    log("[${host ? 'HOST' : 'GAME'}] Generated game args: ${gameArgs.join(" ")}");
    final gameProcess = await startProcess(
        executable: executable,
        args: gameArgs,
        useTempBatch: false,
        name: "${version.content}-${host ? 'HOST' : 'GAME'}",
        environment: {
          "OPENSSL_ia32cap": "~0x20000000"
        }
    );
    final instance = host ? _hostingController.instance.value : _gameController.instance.value;
    void onGameOutput(String line, bool error) {
      log("[${host ? 'HOST' : 'GAME'}] ${error ? '[ERROR]' : '[MESSAGE]'} $line");
      handleGameOutput(
          line: line,
          host: host,
          onShutdown: () => _onStop(reason: _StopReason.normal),
          onTokenError: () => _onStop(reason: _StopReason.tokenError),
          onBuildCorrupted: () {
            if(instance == null) {
              return;
            }else if(!instance.launched) {
              _onStop(reason: _StopReason.corruptedVersionError);
            }else {
              _onStop(reason: _StopReason.crash);
            }
          },
          onLoggedIn: () =>_onLoggedIn(host),
          onMatchEnd: () => _onMatchEnd(version),
          onDisplayAttached: () => _onDisplayAttached(host, hostType, version)
      );
    }
    gameProcess.stdOutput.listen((line) => onGameOutput(line, false));
    gameProcess.stdError.listen((line) => onGameOutput(line, true));
    gameProcess.exitCode.then((_) async {
      final instance = host ? _hostingController.instance.value : _gameController.instance.value;
      log("[${host ? 'HOST' : 'GAME'}] Called exit code(launched: ${instance?.launched}): stop signal");
      _onStop(
          reason: _StopReason.exitCode,
          host: host
      );
    });
    return gameProcess.pid;
  }

  Future<int?> _createPausedProcess(FortniteVersion version, File? file) async {
    if (file == null) {
      return null;
    }

    final process = await startProcess(
        executable: file,
        useTempBatch: false,
        name: "${version.content}-${basenameWithoutExtension(file.path)}",
        environment: {
          "OPENSSL_ia32cap": "~0x20000000"
        }
    );
    final pid = process.pid;
    suspend(pid);
    return pid;
  }

  Future<void> _onDisplayAttached(bool host, GameServerType type, FortniteVersion version) async {
    if(host && type == GameServerType.virtualWindow) {
      final hostingInstance = _hostingController.instance.value;
      if(hostingInstance != null && !hostingInstance.movedToVirtualDesktop) {
        hostingInstance.movedToVirtualDesktop = true;
        try {
          final windowManager = VirtualDesktopManager.getInstance();
          _virtualDesktop = windowManager.createDesktop();
          windowManager.setDesktopName(_virtualDesktop!, "${version.content} Server (Reboot Launcher)");
          var success = false;
          try {
            success = await windowManager.moveWindowToDesktop(
                hostingInstance.gamePid,
                _virtualDesktop!,
                excludedWindowName: "Reboot"
            );
          }catch(error) {
            log("[VIRTUAL_DESKTOP] $error");
            success = false;
          }
          if(!success) {
            try {
              windowManager.removeDesktop(_virtualDesktop!);
            }catch(error) {
              log("[VIRTUAL_DESKTOP] $error");
            }finally {
              _virtualDesktop = null;
            }
          }
        }catch(error) {
          log("[VIRTUAL_DESKTOP] $error");
        }
      }
    }
  }

  void _onMatchEnd(FortniteVersion version) {
    if(_hostingController.autoRestart.value) {
      final notification = LocalNotification(
        title: translations.gameServerEnd,
        body: translations.gameServerRestart(_kRebootDelay.inSeconds),
      );
      notification.show();
      Future.delayed(_kRebootDelay).then((_) async {
        log("[RESTARTER] Stopping server...");
        await _onStop(
            reason: _StopReason.normal,
            host: true
        );
        log("[RESTARTER] Stopped server");
        log("[RESTARTER] Starting server...");
        await _toggle(
            host: true
        );
        log("[RESTARTER] Started server");
      });
    }else {
      final notification = LocalNotification(
          title: translations.gameServerEnd,
          body: translations.gameServerShutdown(_kRebootDelay.inSeconds)
      );
      notification.show();
      Future.delayed(_kRebootDelay).then((_) {
        log("[RESTARTER] Stopping server...");
        _onStop(
            reason: _StopReason.normal,
            host: true
        );
        log("[RESTARTER] Stopped server");
      });
    }
  }

  Future<void> _onLoggedIn(bool host) async {
    final instance = host ? _hostingController.instance.value : _gameController.instance.value;
    if(instance != null && !instance.launched) {
      instance.launched = true;
      instance.tokenError = false;
      await _injectOrShowError(InjectableDll.memoryLeak, host);
      if(!host){
        await _injectOrShowError(InjectableDll.console, host);
        _onGameClientInjected();
      }else {
        final gameServerPort = int.tryParse(_dllController.gameServerPort.text);
        if(gameServerPort != null) {
          await killProcessByPort(gameServerPort);
        }
        await _injectOrShowError(InjectableDll.gameServer, host);
        _onGameServerInjected();
      }
    }
  }

  void _onGameClientInjected() {
    _gameClientInfoBar?.close();
    showRebootInfoBar(
        translations.gameClientStarted,
        severity: InfoBarSeverity.success,
        duration: infoBarLongDuration
    );
  }

  Future<void> _onGameServerInjected() async {
    if(_gameServerInfoBar != null) {
      _gameServerInfoBar?.close();
    }else {
      _gameClientInfoBar?.close();
    }

    final theme = FluentTheme.of(appNavigatorKey.currentContext!);
    try {
      _gameServerInfoBar = showRebootInfoBar(
          translations.waitingForGameServer,
          loading: true,
          duration: null
      );
      final gameServerPort = _dllController.gameServerPort.text;
      final pingOperation = pingGameServerOrTimeout(
          "127.0.0.1:$gameServerPort",
          const Duration(minutes: 2)
      );
      this._pingOperation = pingOperation;
      final localPingResult = await pingOperation.future;
      _gameServerInfoBar?.close();
      if (!localPingResult) {
        showRebootInfoBar(
            translations.gameServerStartWarning,
            severity: InfoBarSeverity.error,
            duration: infoBarLongDuration
        );
        return;
      }
      _backendController.joinLocalhost();
      final accessible = await _checkGameServer(theme, gameServerPort);
      if (!accessible) {
        showRebootInfoBar(
            translations.gameServerStartLocalWarning,
            severity: InfoBarSeverity.warning,
            duration: infoBarLongDuration
        );
        return;
      }

      await _hostingController.publishServer(
        _hostingController.accountUsername.text,
        _hostingController.instance.value!.version.toString(),
      );
      showRebootInfoBar(
          translations.gameServerStarted,
          severity: InfoBarSeverity.success,
          duration: infoBarLongDuration
      );
    }finally {
      _gameServerInfoBar?.close();
    }
  }

  Future<bool> _checkGameServer(FluentThemeData theme, String gameServerPort) async {
    try {
      _gameServerInfoBar = showRebootInfoBar(
          translations.checkingGameServer,
          loading: true,
          duration: null
      );
      final publicIp = await Ipify.ipv4();
      final available = await pingGameServer("$publicIp:$gameServerPort");
      if(available) {
        _gameServerInfoBar?.close();
        return true;
      }

      final pingOperation = pingGameServerOrTimeout(
          "$publicIp:$gameServerPort",
          const Duration(days: 1)
      );
      this._pingOperation = pingOperation;
      _gameServerInfoBar = showRebootInfoBar(
          translations.checkGameServerFixMessage(gameServerPort),
          action: Button(
            onPressed: () => launchUrlString("https://github.com/Auties00/reboot_launcher/blob/master/documentation/$currentLocale/PortForwarding.md"),
            child: Text(translations.checkGameServerFixAction),
          ),
          severity: InfoBarSeverity.warning,
          duration: null,
          loading: true
      );
      final result = await pingOperation.future;
      _gameServerInfoBar?.close();
      return result;
    }finally {
      _gameServerInfoBar?.close();
    }
  }

  Future<void> _onStop({required _StopReason reason, bool? host, String? error, StackTrace? stackTrace}) async {
    if(host == null) {
      try {
        _pingOperation?.complete(false);
      }catch(_) {
        // Ignore: might be running, don't bother checking
      } finally {
        _pingOperation = null;
      }
      await _operation?.cancel();
      _operation = null;
      _backendController.stop();
    }

    host = host ?? widget.host;
    final instance = host ? _hostingController.instance.value : _gameController.instance.value;

    if(host){
      _hostingController.instance.value = null;
    }else {
      _gameController.instance.value = null;
    }

    if(_virtualDesktop != null) {
      try {
        final instance = VirtualDesktopManager.getInstance();
        instance.removeDesktop(_virtualDesktop!);
      }catch(error) {
        log("[VIRTUAL_DESKTOP] Cannot close virtual desktop: $error");
      }
    }

    log("[${host ? 'HOST' : 'GAME'}] Called stop with reason $reason, error data $error $stackTrace");
    log("[${host ? 'HOST' : 'GAME'}] Caller: ${StackTrace.current}");
    if(host) {
      _hostingController.discardServer();
    }

    if(reason == _StopReason.normal) {
      instance?.launched = true;
    }

    instance?.kill();
    final child = instance?.child;
    if(child != null) {
      await _onStop(
          reason: reason,
          host: child.serverType != null
      );
    }

    _setStarted(host, false);
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if(host == true) {
        _gameServerInfoBar?.close();
      }else {
        _gameClientInfoBar?.close();
      }
    });

    switch(reason) {
      case _StopReason.backendError:
      case _StopReason.matchmakerError:
      case _StopReason.normal:
        break;
      case _StopReason.missingVersionError:
        showRebootInfoBar(
          translations.missingVersionError,
          severity: InfoBarSeverity.error,
          duration: infoBarLongDuration,
        );
        break;
      case _StopReason.missingExecutableError:
        showRebootInfoBar(
          translations.missingExecutableError,
          severity: InfoBarSeverity.error,
          duration: infoBarLongDuration,
        );
        break;
      case _StopReason.exitCode:
        if(instance != null && !instance.launched) {
          showRebootInfoBar(
            translations.corruptedVersionError,
            severity: InfoBarSeverity.error,
            duration: infoBarLongDuration,
          );
        }
        break;
      case _StopReason.corruptedVersionError:
        showRebootInfoBar(
            translations.corruptedVersionError,
            severity: InfoBarSeverity.error,
            duration: infoBarLongDuration,
            action: Button(
              onPressed: () => launchUrl(launcherLogFile.uri),
              child: Text(translations.openLog),
            )
        );
        break;
      case _StopReason.corruptedDllError:
        showRebootInfoBar(
          translations.corruptedDllError(error ?? translations.unknownError),
          severity: InfoBarSeverity.error,
          duration: infoBarLongDuration,
        );
        break;
      case _StopReason.missingCustomDllError:
        showRebootInfoBar(
          translations.missingCustomDllError(error!),
          severity: InfoBarSeverity.error,
          duration: infoBarLongDuration,
        );
        break;
      case _StopReason.tokenError:
        _backendController.stop();
        showRebootInfoBar(
            translations.tokenError(instance == null ? translations.none : instance.injectedDlls.map((element) => element.name).join(", ")),
            severity: InfoBarSeverity.error,
            duration: infoBarLongDuration,
            action: Button(
              onPressed: () => launchUrl(launcherLogFile.uri),
              child: Text(translations.openLog),
            )
        );
        break;
      case _StopReason.crash:
        showRebootInfoBar(
          translations.fortniteCrashError(host ? "game server" : "client"),
          severity: InfoBarSeverity.error,
          duration: infoBarLongDuration,
        );
        break;
      case _StopReason.unknownError:
        showRebootInfoBar(
          translations.unknownFortniteError(error ?? translations.unknownError),
          severity: InfoBarSeverity.error,
          duration: infoBarLongDuration,
        );
        break;
    }
  }

  Future<void> _injectOrShowError(InjectableDll injectable, bool hosting) async {
    final instance = hosting ? _hostingController.instance.value : _gameController.instance.value;
    if (instance == null) {
      log("[${hosting ? 'HOST' : 'GAME'}] No instance found to inject ${injectable.name}");
      return;
    }

    try {
      final gameProcess = instance.gamePid;
      log("[${hosting ? 'HOST' : 'GAME'}] Injecting ${injectable.name} into process with pid $gameProcess");
      final dllPath = await _getDllFileOrStop(instance.version, injectable, hosting);
      log("[${hosting ? 'HOST' : 'GAME'}] File to inject for ${injectable.name} at path $dllPath");
      if(dllPath == null) {
        log("[${hosting ? 'HOST' : 'GAME'}] The file doesn't exist");
        _onStop(
            reason: _StopReason.missingCustomDllError,
            error: injectable.name,
            host: hosting
        );
        return;
      }

      log("[${hosting ? 'HOST' : 'GAME'}] Trying to inject ${injectable.name}...");
      await injectDll(gameProcess, dllPath);
      instance.injectedDlls.add(injectable);
      log("[${hosting ? 'HOST' : 'GAME'}] Injected ${injectable.name}");
    } catch (error, stackTrace) {
      log("[${hosting ? 'HOST' : 'GAME'}] Cannot inject ${injectable.name}: $error $stackTrace");
      _onStop(
          reason: _StopReason.corruptedDllError,
          host: hosting,
          error: error.toString(),
          stackTrace: stackTrace
      );
    }
  }

  Future<File?> _getDllFileOrStop(Version version, InjectableDll injectable, bool host, [bool isRetry = false]) async {
    log("[${host ? 'HOST' : 'GAME'}] Checking dll ${injectable}...");
    final (file, customDll) = _dllController.getInjectableData(version, injectable);
    log("[${host ? 'HOST' : 'GAME'}] Path: ${file.path}, custom: $customDll");
    if(await file.exists()) {
      log("[${host ? 'HOST' : 'GAME'}] Path exists");
      return file;
    }

    log("[${host ? 'HOST' : 'GAME'}] Path doesn't exist");
    if(customDll) {
      log("[${host ? 'HOST' : 'GAME'}] Custom dll -> no recovery");
      _onStop(
        reason: _StopReason.missingCustomDllError,
        error: injectable.name,
      );
      return null;
    }

    log("[${host ? 'HOST' : 'GAME'}] Path does not exist, downloading critical dll again...");
    await _dllController.download(injectable, file.path, force: true);
    log("[${host ? 'HOST' : 'GAME'}] Downloaded dll again, retrying check...");
    return _getDllFileOrStop(version, injectable, host, true);
  }

  InfoBarEntry _showLaunchingGameServerWidget() => _gameServerInfoBar = showRebootInfoBar(
      translations.launchingGameServer,
      loading: true,
      duration: null
  );

  InfoBarEntry _showLaunchingGameClientWidget(FortniteVersion version, GameServerType hostType, bool linkedHosting) {
    return _gameClientInfoBar = showRebootInfoBar(
        linkedHosting ? translations.launchingGameClientAndServer : translations.launchingGameClientOnly,
        loading: true,
        duration: null,
        action: Obx(() {
          if(_hostingController.started.value || linkedHosting) {
            return const SizedBox.shrink();
          }

          return Padding(
            padding: const EdgeInsets.only(
                bottom: 2.0
            ),
            child: Button(
              onPressed: () async {
                _backendController.joinLocalhost();
                if(!_hostingController.started.value) {
                  _gameController.instance.value?.child = await _startMatchMakingServer(version, false, hostType, true);
                  _gameClientInfoBar?.close();
                  _showLaunchingGameClientWidget(version, hostType, true);
                }
              },
              child: Text(translations.startGameServer),
            ),
          );
        })
    );
  }
}

enum _StopReason {
  normal,
  missingVersionError,
  missingExecutableError,
  corruptedVersionError,
  missingCustomDllError,
  corruptedDllError,
  backendError,
  matchmakerError,
  tokenError,
  unknownError,
  exitCode,
  crash;

  bool get isError => name.contains("Error");
}