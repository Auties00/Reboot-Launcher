import 'dart:async';
import 'dart:io';

import 'package:async/async.dart';
import 'package:dart_ipify/dart_ipify.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:get/get.dart';
import 'package:local_notifier/local_notifier.dart';
import 'package:path/path.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/backend_controller.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/hosting_controller.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/dialog/abstract/dialog.dart';
import 'package:reboot_launcher/src/dialog/abstract/dialog_button.dart';
import 'package:reboot_launcher/src/dialog/abstract/info_bar.dart';
import 'package:reboot_launcher/src/dialog/implementation/server.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/page/pages.dart';
import 'package:reboot_launcher/src/util/dll.dart';
import 'package:reboot_launcher/src/util/log.dart';
import 'package:reboot_launcher/src/util/matchmaker.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/util/translations.dart';

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
  final SettingsController _settingsController = Get.find<SettingsController>();
  InfoBarEntry? _gameClientInfoBar;
  InfoBarEntry? _gameServerInfoBar;
  CancelableOperation? _operation;
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
      if(await _getDllFileOrStop(injectable, host) == null) {
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
      final serverType =  _hostingController.type.value;
      log("[${host ? 'HOST' : 'GAME'}] Implicit game server metadata: headless($serverType)");
      final linkedHostingInstance = await _startMatchMakingServer(version, host, serverType, false);
      log("[${host ? 'HOST' : 'GAME'}] Implicit game server result: $linkedHostingInstance");
      await _startGameProcesses(version, host, serverType, linkedHostingInstance);
      if(!host) {
        _showLaunchingGameClientWidget();
      }

      if(linkedHostingInstance != null || host){
        _showLaunchingGameServerWidget();
      }
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

    if(_backendController.type.value == ServerType.embedded && !isLocalHost(_backendController.gameServerAddress.text)) {
      log("[${host ? 'HOST' : 'GAME'}] Backend is not set to embedded and/or not pointing to the local game server");
      return null;
    }

    if(_hostingController.started()){
      log("[${host ? 'HOST' : 'GAME'}] The user has already manually started the hosting server");
      return null;
    }

    final response = forceLinkedHosting || await _askForAutomaticGameServer();
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

  Future<bool> _askForAutomaticGameServer() async {
    final result = await showAppDialog<bool>(
        builder: (context) => InfoDialog(
          text: "The launcher detected that you are not running a game server, but that your matchmaker is set to your local machine. "
              "If you don't want to join another player's server, you should start a game server. This is necessary to be able to play: for more information check the Info tab in the launcher.",
          buttons: [
            DialogButton(
                type: ButtonType.secondary,
                text: "Ignore"
            ),
            DialogButton(
              type: ButtonType.primary,
              text: "Start server",
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
        versionName: version.name,
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
    await _injectOrShowError(InjectableDll.cobalt, host);
    log("[${host ? 'HOST' : 'GAME'}] Finished creating game instance");
    return instance;
  }

  Future<int?> _createGameProcess(FortniteVersion version, File executable, bool host, GameServerType hostType, GameInstance? linkedHosting) async {
    log("[${host ? 'HOST' : 'GAME'}] Generating instance args...");
    final gameArgs = createRebootArgs(
        _gameController.username.text,
        _gameController.password.text,
        host,
        hostType,
        ""
    );
    log("[${host ? 'HOST' : 'GAME'}] Generated game args: ${gameArgs.join(" ")}");
    final gameProcess = await startProcess(
        executable: executable,
        args: gameArgs,
        useTempBatch: false,
        name: "${version.name}-${host ? 'HOST' : 'GAME'}"
    );
    void onGameOutput(String line, bool error) {
      log("[${host ? 'HOST' : 'GAME'}] ${error ? '[ERROR]' : '[MESSAGE]'} $line");

      handleGameOutput(
          line: line,
          host: host,
          onShutdown: () => _onStop(reason: _StopReason.normal),
          onTokenError: () => _onStop(reason: _StopReason.tokenError),
          onBuildCorrupted: () => _onStop(reason: _StopReason.corruptedVersionError),
          onLoggedIn: () =>_onLoggedIn(host),
          onMatchEnd: () => _onMatchEnd(version),
          onDisplayAttached: () => _onDisplayAttached(host, hostType, version)
      );
    }
    gameProcess.stdOutput.listen((line) => onGameOutput(line, false));
    gameProcess.stdError.listen((line) => onGameOutput(line, true));
    gameProcess.exitCode.then((_) async {
      final instance = host ? _hostingController.instance.value : _gameController.instance.value;
      if(instance == null) {
        log("[${host ? 'HOST' : 'GAME'}] Called exit code, but the game process is no longer running");
        return;
      }

      log("[${host ? 'HOST' : 'GAME'}] Called exit code(launched: ${instance.launched}): stop signal");
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
        name: "${version.name}-${basenameWithoutExtension(file.path)}"
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
          windowManager.setDesktopName(_virtualDesktop!, "${version.name} Server (Reboot Launcher)");
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
      await _injectOrShowError(InjectableDll.memory, host);
      if(!host){
        await _injectOrShowError(InjectableDll.console, host);
        _onGameClientInjected();
      }else {
        final gameServerPort = int.tryParse(_settingsController.gameServerPort.text);
        if(gameServerPort != null) {
          await killProcessByPort(gameServerPort);
        }
        await _injectOrShowError(InjectableDll.reboot, host);
        _onGameServerInjected();
      }
    }
  }

  void _onGameClientInjected() {
    _gameClientInfoBar?.close();
    showInfoBar(
        translations.gameClientStarted,
        severity: InfoBarSeverity.success,
        duration: infoBarLongDuration
    );
  }

  Future<void> _onGameServerInjected() async {
    _gameServerInfoBar?.close();
    final theme = FluentTheme.of(appKey.currentContext!);
    try {
      _gameServerInfoBar = showInfoBar(
          translations.waitingForGameServer,
          loading: true,
          duration: null
      );
      final gameServerPort = _settingsController.gameServerPort.text;
      final localPingResult = await pingGameServer(
          "127.0.0.1:$gameServerPort",
          timeout: const Duration(minutes: 2)
      );
      _gameServerInfoBar?.close();
      if (!localPingResult) {
        showInfoBar(
            translations.gameServerStartWarning,
            severity: InfoBarSeverity.error,
            duration: infoBarLongDuration
        );
        return;
      }
      _backendController.joinLocalHost();
      final accessible = await _checkGameServer(theme, gameServerPort);
      if (!accessible) {
        showInfoBar(
            translations.gameServerStartLocalWarning,
            severity: InfoBarSeverity.warning,
            duration: infoBarLongDuration
        );
        return;
      }

      await _hostingController.publishServer(
        _gameController.username.text,
        _hostingController.instance.value!.versionName,
      );
      showInfoBar(
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
      _gameServerInfoBar = showInfoBar(
          translations.checkingGameServer,
          loading: true,
          duration: null
      );
      final publicIp = await Ipify.ipv4();
      final externalResult = await pingGameServer("$publicIp:$gameServerPort");
      if (externalResult) {
        return true;
      }

      _gameServerInfoBar?.close();
      final future = pingGameServer(
          "$publicIp:$gameServerPort",
          timeout: const Duration(days: 365)
      );
      _gameServerInfoBar = showInfoBar(
          translations.checkGameServerFixMessage(gameServerPort),
          action: Button(
            onPressed: () async {
              pageIndex.value = RebootPageType.info.index;
            },
            child: Text(translations.checkGameServerFixAction),
          ),
          severity: InfoBarSeverity.warning,
          duration: null,
          loading: true
      );
      return await future;
    }finally {
      _gameServerInfoBar?.close();
    }
  }

  Future<void> _onStop({required _StopReason reason, bool? host, String? error, StackTrace? stackTrace}) async {
    if(host == null) {
      await _operation?.cancel();
      _operation = null;
      await _backendController.worker?.cancel();
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

    if(instance != null) {
      if(reason == _StopReason.normal) {
        instance.launched = true;
      }

      instance.kill();
      final child = instance.child;
      if(child != null) {
        await _onStop(
            reason: reason,
            host: child.serverType != null
        );
      }
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
        showInfoBar(
          translations.missingVersionError,
          severity: InfoBarSeverity.error,
          duration: infoBarLongDuration,
        );
        break;
      case _StopReason.missingExecutableError:
        showInfoBar(
          translations.missingExecutableError,
          severity: InfoBarSeverity.error,
          duration: infoBarLongDuration,
        );
        break;
      case _StopReason.exitCode:
        if(instance != null && !instance.launched) {
          showInfoBar(
            translations.corruptedVersionError,
            severity: InfoBarSeverity.error,
            duration: infoBarLongDuration,
          );
        }
        break;
      case _StopReason.corruptedVersionError:
        showInfoBar(
          translations.corruptedVersionError,
          severity: InfoBarSeverity.error,
          duration: infoBarLongDuration,
        );
        break;
      case _StopReason.corruptedDllError:
        showInfoBar(
          translations.corruptedDllError(error!),
          severity: InfoBarSeverity.error,
          duration: infoBarLongDuration,
        );
        break;
      case _StopReason.tokenError:
        showInfoBar(
          translations.tokenError(instance?.injectedDlls.map((element) => element.name).join(", ") ?? "none"),
          severity: InfoBarSeverity.error,
          duration: infoBarLongDuration,
        );
        break;
      case _StopReason.unknownError:
        showInfoBar(
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
      final dllPath = await _getDllFileOrStop(injectable, hosting);
      log("[${hosting ? 'HOST' : 'GAME'}] File to inject for ${injectable.name} at path $dllPath");
      if(dllPath == null) {
        log("[${hosting ? 'HOST' : 'GAME'}] The file doesn't exist");
        _onStop(
            reason: _StopReason.corruptedDllError,
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

  Future<File?> _getDllFileOrStop(InjectableDll injectable, bool host) async {
    log("[${host ? 'HOST' : 'GAME'}] Checking dll ${injectable}...");
    final path = injectable.path;
    log("[${host ? 'HOST' : 'GAME'}] Path: $path");
    final file = File(path);
    if(await file.exists()) {
      log("[${host ? 'HOST' : 'GAME'}] Path exists");
      return file;
    }

    log("[${host ? 'HOST' : 'GAME'}] Path does not exist, downloading critical dll again...");
    await downloadCriticalDllInteractive(path);
    log("[${host ? 'HOST' : 'GAME'}] Downloaded dll again, retrying check...");
    return _getDllFileOrStop(injectable, host);
  }

  InfoBarEntry _showLaunchingGameServerWidget() => _gameServerInfoBar = showInfoBar(
      translations.launchingHeadlessServer,
      loading: true,
      duration: null
  );

  InfoBarEntry _showLaunchingGameClientWidget() => _gameClientInfoBar = showInfoBar(
      translations.launchingGameClient,
      loading: true,
      duration: null
  );
}

enum _StopReason {
  normal,
  missingVersionError,
  missingExecutableError,
  corruptedVersionError,
  corruptedDllError,
  backendError,
  matchmakerError,
  tokenError,
  unknownError,
  exitCode;

  bool get isError => name.contains("Error");
}