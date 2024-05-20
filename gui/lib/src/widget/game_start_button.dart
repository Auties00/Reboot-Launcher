import 'dart:async';
import 'dart:io';

import 'package:async/async.dart';
import 'package:dart_ipify/dart_ipify.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:get/get.dart';
import 'package:process_run/shell.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/authenticator_controller.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/hosting_controller.dart';
import 'package:reboot_launcher/src/controller/matchmaker_controller.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/dialog/abstract/info_bar.dart' as messenger;
import 'package:reboot_launcher/src/dialog/abstract/info_bar.dart';
import 'package:reboot_launcher/src/dialog/implementation/server.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/page/pages.dart';
import 'package:reboot_launcher/src/util/daemon.dart';
import 'package:reboot_launcher/src/util/dll.dart';
import 'package:reboot_launcher/src/util/matchmaker.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/util/tutorial.dart';

class LaunchButton extends StatefulWidget {
  final bool host;
  final String? startLabel;
  final String? stopLabel;

  const LaunchButton({Key? key, required this.host, this.startLabel, this.stopLabel}) : super(key: key);

  @override
  State<LaunchButton> createState() => _LaunchButtonState();
}

class _LaunchButtonState extends State<LaunchButton> {
  final GameController _gameController = Get.find<GameController>();
  final HostingController _hostingController = Get.find<HostingController>();
  final AuthenticatorController _authenticatorController = Get.find<AuthenticatorController>();
  final MatchmakerController _matchmakerController = Get.find<MatchmakerController>();
  final SettingsController _settingsController = Get.find<SettingsController>();
  CancelableOperation? _operation;

  @override
  Widget build(BuildContext context) => Align(
    alignment: AlignmentDirectional.bottomCenter,
    child: SizedBox(
      width: double.infinity,
      child: Obx(() => SizedBox(
        height: 48,
        child: Button(
          onPressed: () => _operation = CancelableOperation.fromFuture(_start()),
          child: Align(
            alignment: Alignment.center,
            child: Text(_hasStarted ? _stopMessage : _startMessage)
          )
        ),
      )),
    ),
  );

  bool get _hasStarted => widget.host ? _hostingController.started() : _gameController.started();

  void _setStarted(bool hosting, bool started) => hosting ? _hostingController.started.value = started : _gameController.started.value = started;

  String get _startMessage => widget.startLabel ?? (widget.host ? translations.startHosting : translations.startGame);

  String get _stopMessage => widget.stopLabel ?? (widget.host ? translations.stopHosting : translations.stopGame);

  Future<void> _start() async {
    if (_hasStarted) {
      _onStop(
        reason: _StopReason.normal
      );
      return;
    }
    
    if(_operation != null) {
      return;
    }

    if(_gameController.selectedVersion == null){
      _onStop(
          reason: _StopReason.missingVersionError
      );
      return;
    }

    _setStarted(widget.host, true);
    for (final injectable in _Injectable.values) {
      if(await _getDllFileOrStop(injectable, widget.host) == null) {
        return;
      }
    }

    try {
      final version = _gameController.selectedVersion!;
      final executable = await version.executable;
      if(executable == null){
        _onStop(
            reason: _StopReason.missingExecutableError,
            error: version.location.path
        );
        return;
      }

      final authenticatorResult = _authenticatorController.started() || await _authenticatorController.toggleInteractive(false);
      if(!authenticatorResult){
        _onStop(
            reason: _StopReason.authenticatorError
        );
        return;
      }

      final matchmakerResult = _matchmakerController.started() || await _matchmakerController.toggleInteractive(false);
      if(!matchmakerResult){
        _onStop(
            reason: _StopReason.matchmakerError
        );
        return;
      }

      final linkedHostingInstance = await _startMatchMakingServer(version);
      await _startGameProcesses(version, widget.host, linkedHostingInstance);
      if(linkedHostingInstance != null || widget.host){
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

  Future<GameInstance?> _startMatchMakingServer(FortniteVersion version) async {
    if(widget.host){
      return null;
    }

    final matchmakingIp = _matchmakerController.gameServerAddress.text;
    if(!isLocalHost(matchmakingIp)) {
      return null;
    }

    if(_hostingController.started()){
      return null;
    }
    
    if(!_hostingController.automaticServer()) {
      return null;
    }

    final instance = await _startGameProcesses(version, true, null);
    _setStarted(true, true);
    return instance;
  }

  Future<GameInstance?> _startGameProcesses(FortniteVersion version, bool host, GameInstance? linkedHosting) async {
    final launcherProcess = await _createLauncherProcess(version);
    final eacProcess = await _createEacProcess(version);
    final executable = await version.executable;
    final gameProcess = await _createGameProcess(executable!.path, host);
    if(gameProcess == null) {
      return null;
    }

    final instance = GameInstance(
        versionName: version.name,
        gamePid: gameProcess,
        launcherPid: launcherProcess,
        eacPid: eacProcess,
        hosting: host,
        child: linkedHosting
    );
    instance.startObserver();
    if(host){
      _hostingController.discardServer();
      _hostingController.instance.value = instance;
    }else{
      _gameController.instance.value = instance;
    }
    _injectOrShowError(_Injectable.sslBypass, host);
    return instance;
  }

  Future<int?> _createGameProcess(String gamePath, bool host) async {
    if(!_hasStarted) {
      return null;
    }

    final gameArgs = createRebootArgs(
        _gameController.username.text,
        _gameController.password.text,
        host,
        _hostingController.headless.value,
        _gameController.customLaunchArgs.text
    );
    final gameProcess = await Process.start(
        gamePath,
        gameArgs
    );
    gameProcess
      ..exitCode.then((_) => _onStop(reason: _StopReason.exitCode))
      ..outLines.forEach((line) => _onGameOutput(line, host, false))
      ..errLines.forEach((line) => _onGameOutput(line, host, true));
    return gameProcess.pid;
  }

  Future<int?> _createLauncherProcess(FortniteVersion version) async {
    final launcherFile = version.launcher;
    if (launcherFile == null) {
      return null;
    }

    final launcherProcess = await Process.start(launcherFile.path, []);
    final pid = launcherProcess.pid;
    suspend(pid);
    return pid;
  }

  Future<int?> _createEacProcess(FortniteVersion version) async {
    final eacFile = version.eacExecutable;
    if (eacFile == null) {
      return null;
    }

    final eacProcess = await Process.start(eacFile.path, []);
    final pid = eacProcess.pid;
    suspend(pid);
    return pid;
  }

  void _onGameOutput(String line, bool host, bool error) {
    if(kDebugMode) {
      print("${error ? '[ERROR]' : '[MESSAGE]'} $line");
    }

    if (line.contains(kShutdownLine)) {
      _onStop(
          reason: _StopReason.normal
      );
      return;
    }

    if(kCorruptedBuildErrors.any((element) => line.contains(element))){
      _onStop(
          reason: _StopReason.corruptedVersionError
      );
      return;
    }

    if(kCannotConnectErrors.any((element) => line.contains(element))){
      _onStop(
          reason: _StopReason.tokenError
      );
      return;
    }

    if(line.contains("Region ")){
      if(!host){
        _injectOrShowError(_Injectable.console, host);
      }else {
        _injectOrShowError(_Injectable.reboot, host)
            .then((value) => _onGameServerInjected());
      }

      _injectOrShowError(_Injectable.memoryFix, host);
      final instance = host ? _hostingController.instance.value : _gameController.instance.value;
      instance?.launched = true;
      instance?.tokenError = false;
    }
  }

  Future<void> _onGameServerInjected() async {
    final theme = FluentTheme.of(appKey.currentContext!);
    showInfoBar(
        translations.waitingForGameServer,
        loading: true,
        duration: null
    );
    final gameServerPort = _settingsController.gameServerPort.text;
    final localPingResult = await pingGameServer(
        "127.0.0.1:$gameServerPort",
        timeout: const Duration(minutes: 2)
    );
    if(!localPingResult) {
      showInfoBar(
          translations.gameServerStartWarning,
          severity: InfoBarSeverity.error,
          duration: infoBarLongDuration
      );
      return;
    }

    _matchmakerController.joinLocalHost();
    final accessible = await _checkGameServer(theme, gameServerPort);
    if(!accessible) {
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
  }

  Future<bool> _checkGameServer(FluentThemeData theme, String gameServerPort) async {
    showInfoBar(
        translations.checkingGameServer,
        loading: true,
        duration: null
    );
    final publicIp = await Ipify.ipv4();
    final externalResult = await pingGameServer("$publicIp:$gameServerPort");
    if(externalResult) {
      return true;
    }

    final future = pingGameServer(
        "$publicIp:$gameServerPort",
        timeout: const Duration(days: 365)
    );
    showInfoBar(
        translations.checkGameServerFixMessage(gameServerPort),
        action: Button(
          onPressed: openPortTutorial,
          child: Text(translations.checkGameServerFixAction),
        ),
        severity: InfoBarSeverity.warning,
        duration: null,
        loading: true
    );
    return await future;
  }

  void _onStop({required _StopReason reason, bool? host, String? error, StackTrace? stackTrace}) async {
    host = host ?? widget.host;
    await _operation?.cancel();
    await _authenticatorController.worker?.cancel();
    await _matchmakerController.worker?.cancel();
    final instance = host ? _hostingController.instance.value : _gameController.instance.value;
    if(instance != null){
      _onStop(
          reason: _StopReason.normal,
          host: true
      );
      instance.kill();
      if(host){
        _hostingController.instance.value = null;
      }else {
        _gameController.instance.value = null;
      }
    }

    _setStarted(host, false);
    if(host){
      _hostingController.discardServer();
    }

    if(reason == _StopReason.normal) {
      messenger.removeMessageByPage(_pageType.index);
    }

    switch(reason) {
      case _StopReason.authenticatorError:
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
        final instance = host ? _hostingController.instance.value : _gameController.instance.value;
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
          translations.tokenError,
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
    _operation = null;
  }

  Future<void> _injectOrShowError(_Injectable injectable, bool hosting) async {
    final instance = hosting ? _hostingController.instance.value : _gameController.instance.value;
    if (instance == null) {
      return;
    }

    try {
      final gameProcess = instance.gamePid;
      final dllPath = await _getDllFileOrStop(injectable, hosting);
      if(dllPath == null) {
        return;
      }

      await injectDll(gameProcess, dllPath.path);
    } catch (error, stackTrace) {
      _onStop(
          reason: _StopReason.corruptedDllError,
          host: hosting,
          error: error.toString(),
          stackTrace: stackTrace
      );
    }
  }

  String _getDllPath(_Injectable injectable) {
    switch(injectable){
      case _Injectable.reboot:
        return _settingsController.gameServerDll.text;
      case _Injectable.console:
        return _settingsController.unrealEngineConsoleDll.text;
      case _Injectable.sslBypass:
        return _settingsController.authenticatorDll.text;
      case _Injectable.memoryFix:
        return _settingsController.memoryLeakDll.text;
    }
  }
  
  Future<File?> _getDllFileOrStop(_Injectable injectable, bool host) async {
    final path = _getDllPath(injectable);
    final file = File(path);
    if(await file.exists()) {
      return file;
    }

    await downloadCriticalDllInteractive(path);
    return null;
  }

  OverlayEntry _showLaunchingGameServerWidget() => showInfoBar(
      translations.launchingHeadlessServer,
      loading: true,
      duration: null
  );

  RebootPageType get _pageType => widget.host ? RebootPageType.host : RebootPageType.play;
}

enum _StopReason {
  normal,
  missingVersionError,
  missingExecutableError,
  corruptedVersionError,
  corruptedDllError,
  authenticatorError,
  matchmakerError,
  tokenError,
  unknownError, exitCode
}

enum _Injectable {
  console,
  sslBypass,
  reboot,
  memoryFix
}
