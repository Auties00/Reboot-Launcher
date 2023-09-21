import 'dart:async';
import 'dart:io';

import 'package:async/async.dart';
import 'package:dart_ipify/dart_ipify.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:process_run/shell.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/authenticator_controller.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/hosting_controller.dart';
import 'package:reboot_launcher/src/controller/matchmaker_controller.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/dialog/abstract/info_bar.dart' as messenger;
import 'package:reboot_launcher/src/dialog/implementation/server.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/page/pages.dart';
import 'package:reboot_launcher/src/util/matchmaker.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/util/tutorial.dart';
import 'package:reboot_launcher/src/util/watch.dart';

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
    for (var injectable in _Injectable.values) {
      if(await _getDllFileOrStop(injectable, widget.host) == null) {
        return;
      }
    }

    try {
      var version = _gameController.selectedVersion!;
      var executable = await version.executable;
      if(executable == null){
        _onStop(
            reason: _StopReason.missingExecutableError,
            error: version.location.path
        );
        return;
      }

      var authenticatorResult = _authenticatorController.started() || await _authenticatorController.toggleInteractive(_pageType, false);
      if(!authenticatorResult){
        _onStop(
            reason: _StopReason.authenticatorError
        );
        return;
      }

      var matchmakerResult = _matchmakerController.started() || await _matchmakerController.toggleInteractive(_pageType, false);
      if(!matchmakerResult){
        _onStop(
            reason: _StopReason.matchmakerError
        );
        return;
      }

      var automaticallyStartedServer = await _startMatchMakingServer(version);
      await _startGameProcesses(version, widget.host, automaticallyStartedServer);
      if(automaticallyStartedServer || widget.host){
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

  Future<bool> _startMatchMakingServer(FortniteVersion version) async {
    if(widget.host){
      return false;
    }

    var matchmakingIp = _matchmakerController.gameServerAddress.text;
    if(!isLocalHost(matchmakingIp)) {
      return false;
    }

    if(_hostingController.started()){
      return false;
    }

    _startGameProcesses(version, true, false); // Do not await
    _setStarted(true, true);
    return true;
  }

  Future<void> _startGameProcesses(FortniteVersion version, bool host, bool linkedHosting) async {
    var launcherProcess = await _createLauncherProcess(version);
    var eacProcess = await _createEacProcess(version);
    var executable = await version.executable;
    var gameProcess = await _createGameProcess(executable!.path, host);
    if(gameProcess == null) {
      return;
    }

    var instance = GameInstance(version.name, gameProcess, launcherProcess, eacProcess, host, linkedHosting);
    instance.startObserver();
    if(host){
      _hostingController.discardServer();
      _hostingController.instance.value = instance;
      _hostingController.saveInstance();
    }else{
      _gameController.instance.value = instance;
      _gameController.saveInstance();
    }
    _injectOrShowError(_Injectable.sslBypass, host);
  }

  Future<int?> _createGameProcess(String gamePath, bool host) async {
    if(!_hasStarted) {
      return null;
    }

    var gameArgs = createRebootArgs(
        _gameController.username.text,
        _gameController.password.text,
        host,
        _gameController.customLaunchArgs.text
    );
    var gameProcess = await Process.start(
        gamePath,
        gameArgs
    );
    gameProcess
      ..exitCode.then((_) => _onStop(reason: _StopReason.normal))
      ..outLines.forEach((line) => _onGameOutput(line, host))
      ..errLines.forEach((line) => _onGameOutput(line, host));
    return gameProcess.pid;
  }

  Future<int?> _createLauncherProcess(FortniteVersion version) async {
    var launcherFile = version.launcher;
    if (launcherFile == null) {
      return null;
    }

    var launcherProcess = await Process.start(launcherFile.path, []);
    var pid = launcherProcess.pid;
    suspend(pid);
    return pid;
  }

  Future<int?> _createEacProcess(FortniteVersion version) async {
    var eacFile = version.eacExecutable;
    if (eacFile == null) {
      return null;
    }

    var eacProcess = await Process.start(eacFile.path, []);
    var pid = eacProcess.pid;
    suspend(pid);
    return pid;
  }

  void _onGameOutput(String line, bool host) {
    if (line.contains(shutdownLine)) {
      _onStop(
          reason: _StopReason.normal
      );
      return;
    }

    if(corruptedBuildErrors.any((element) => line.contains(element))){
      _onStop(
          reason: _StopReason.corruptedVersionError
      );
      return;
    }

    if(cannotConnectErrors.any((element) => line.contains(element))){
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
      var instance = host ? _hostingController.instance.value : _gameController.instance.value;
      instance?.tokenError = false;
    }
  }

  Future<void> _onGameServerInjected() async {
    var theme = FluentTheme.of(appKey.currentContext!);
    showInfoBar(
        translations.waitingForGameServer,
        loading: true,
        duration: null
    );
    var gameServerPort = _settingsController.gameServerPort.text;
    var localPingResult = await pingGameServer(
        "127.0.0.1:$gameServerPort",
        timeout: const Duration(minutes: 1)
    );
    if(!localPingResult) {
      showInfoBar(
          translations.gameServerStartWarning,
          severity: InfoBarSeverity.error,
          duration: snackbarLongDuration
      );
      return;
    }

    _matchmakerController.joinLocalHost();
    var accessible = await _checkGameServer(theme, gameServerPort);
    if(!accessible) {
      showInfoBar(
          translations.gameServerStartLocalWarning,
          severity: InfoBarSeverity.warning,
          duration: snackbarLongDuration
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
        duration: snackbarLongDuration
    );
  }

  Future<bool> _checkGameServer(FluentThemeData theme, String gameServerPort) async {
    showInfoBar(
        translations.checkingGameServer,
        loading: true,
        duration: null
    );
    var publicIp = await Ipify.ipv4();
    var externalResult = await pingGameServer("$publicIp:$gameServerPort");
    if(externalResult) {
      return true;
    }

    var future = pingGameServer(
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
    var instance = host ? _hostingController.instance.value : _gameController.instance.value;
    if(instance != null){
      if(instance.linkedHosting){
        _onStop(
          reason: _StopReason.normal,
          host: true
        );
      }

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

    messenger.removeMessageByPage(_pageType.index);
    switch(reason) {
      case _StopReason.authenticatorError:
      case _StopReason.matchmakerError:
      case _StopReason.normal:
        break;
      case _StopReason.missingVersionError:
        showInfoBar(
          translations.missingVersionError,
          severity: InfoBarSeverity.error,
          duration: snackbarLongDuration,
        );
        break;
      case _StopReason.missingExecutableError:
        showInfoBar(
          translations.missingExecutableError,
          severity: InfoBarSeverity.error,
          duration: snackbarLongDuration,
        );
        break;
      case _StopReason.corruptedVersionError:
        showInfoBar(
          translations.corruptedVersionError,
          severity: InfoBarSeverity.error,
          duration: snackbarLongDuration,
        );
        break;
      case _StopReason.missingDllError:
        showInfoBar(
          translations.missingDllError(error!),
          severity: InfoBarSeverity.error,
          duration: snackbarLongDuration,
        );
        break;
      case _StopReason.corruptedDllError:
        showInfoBar(
          translations.corruptedDllError(error!),
          severity: InfoBarSeverity.error,
          duration: snackbarLongDuration,
        );
        break;
      case _StopReason.tokenError:
        showInfoBar(
          translations.tokenError,
          severity: InfoBarSeverity.error,
          duration: snackbarLongDuration,
        );
        break;
      case _StopReason.unknownError:
        showInfoBar(
          translations.unknownFortniteError(error ?? translations.unknownError),
          severity: InfoBarSeverity.error,
          duration: snackbarLongDuration,
        );
        break;
    }
    _operation = null;
  }

  Future<void> _injectOrShowError(_Injectable injectable, bool hosting) async {
    var instance = hosting ? _hostingController.instance.value : _gameController.instance.value;
    if (instance == null) {
      return;
    }

    try {
      var gameProcess = instance.gamePid;
      var dllPath = await _getDllFileOrStop(injectable, hosting);
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
    var path = _getDllPath(injectable);
    var file = File(path);
    if(await file.exists()) {
      return file;
    }

    _onStop(
        reason: _StopReason.missingDllError,
        host: host,
        error: path
    );
    return null;
  }

  OverlayEntry _showLaunchingGameServerWidget() => showInfoBar(
      translations.launchingHeadlessServer,
      loading: true,
      duration: null
  );

  OverlayEntry showInfoBar(dynamic text, {InfoBarSeverity severity = InfoBarSeverity.info, bool loading = false, Duration? duration = snackbarShortDuration, Widget? action}) => messenger.showInfoBar(
      text,
      pageType: _pageType,
      severity: severity,
      loading: loading,
      duration: duration,
      action: action
  );

  RebootPageType get _pageType => widget.host ? RebootPageType.host : RebootPageType.play;
}

enum _StopReason {
  normal,
  missingVersionError,
  missingExecutableError,
  corruptedVersionError,
  missingDllError,
  corruptedDllError,
  authenticatorError,
  matchmakerError,
  tokenError,
  unknownError
}

enum _Injectable {
  console,
  sslBypass,
  reboot,
  memoryFix
}
