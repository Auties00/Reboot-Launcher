import 'dart:async';
import 'dart:io';

import 'package:async/async.dart';
import 'package:dart_ipify/dart_ipify.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/gestures.dart';
import 'package:get/get.dart';
import 'package:path/path.dart' as path;
import 'package:process_run/shell.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/authenticator_controller.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/hosting_controller.dart';
import 'package:reboot_launcher/src/controller/matchmaker_controller.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/dialog/abstract/info_bar.dart';
import 'package:reboot_launcher/src/dialog/implementation/game.dart';
import 'package:reboot_launcher/src/dialog/implementation/server.dart';
import 'package:reboot_launcher/src/util/matchmaker.dart';
import 'package:reboot_launcher/src/util/tutorial.dart';
import 'package:reboot_launcher/src/util/watch.dart';
import 'package:supabase_flutter/supabase_flutter.dart';

class LaunchButton extends StatefulWidget {
  final bool host;
  final String? startLabel;
  final String? stopLabel;

  const LaunchButton({Key? key, required this.host, this.startLabel, this.stopLabel}) : super(key: key);

  @override
  State<LaunchButton> createState() => _LaunchButtonState();
}

class _LaunchButtonState extends State<LaunchButton> {
  final SupabaseClient _supabase = Supabase.instance.client;
  final GameController _gameController = Get.find<GameController>();
  final HostingController _hostingController = Get.find<HostingController>();
  final AuthenticatorController _authenticatorController = Get.find<AuthenticatorController>();
  final MatchmakerController _matchmakerController = Get.find<MatchmakerController>();
  final SettingsController _settingsController = Get.find<SettingsController>();
  final File _logFile = File("${logsDirectory.path}\\game.log");
  bool _fail = false;

  @override
  Widget build(BuildContext context) => Align(
    alignment: AlignmentDirectional.bottomCenter,
    child: SizedBox(
      width: double.infinity,
      child: Obx(() => SizedBox(
        height: 48,
        child: Button(
          onPressed: _start,
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

  String get _startMessage => widget.startLabel ?? (widget.host ? "Start hosting" : "Launch fortnite");

  String get _stopMessage => widget.stopLabel ?? (widget.host ? "Stop hosting" : "Close fortnite");

  Future<void> _start() async {
    if (_hasStarted) {
      _onStop(widget.host, false);
      removeMessage(widget.host ? 1 : 0);
      return;
    }

    _fail = false;
    if(_gameController.selectedVersion == null){
      showInfoBar("Select a Fortnite version before continuing");
      _onStop(widget.host, false);
      return;
    }

    _setStarted(widget.host, true);
    for (var element in Injectable.values) {
      if(await _getDllPath(element, widget.host) == null) {
        return;
      }
    }

    try {
      var version = _gameController.selectedVersion!;
      var executable = await version.executable;
      if(executable == null){
        showMissingBuildError(version);
        _onStop(widget.host, false);
        return;
      }

      var authenticatorResult = _authenticatorController.started() || await _authenticatorController.toggleInteractive(false);
      if(!authenticatorResult){
        _onStop(widget.host, false);
        return;
      }

      var matchmakerResult = _matchmakerController.started() || await _matchmakerController.toggleInteractive(false);
      if(!matchmakerResult){
        _onStop(widget.host, false);
        return;
      }

      var automaticallyStartedServer = await _startMatchMakingServer();
      await _startGameProcesses(version, widget.host, automaticallyStartedServer);
      if(widget.host){
        showInfoBar(
            "Launching the headless server...",
            loading: true,
            duration: null
        );
      }
    } catch (exception, stacktrace) {
      _onStop(widget.host, false);
      showCorruptedBuildError(widget.host, exception, stacktrace);
    }
  }

  Future<void> _startGameProcesses(FortniteVersion version, bool host, bool linkedHosting) async {
    _setStarted(host, true);
    var launcherProcess = await _createLauncherProcess(version);
    var eacProcess = await _createEacProcess(version);
    var executable = await version.executable;
    var gameProcess = await _createGameProcess(executable!.path, host);
    var instance = GameInstance(version.name, gameProcess, launcherProcess, eacProcess, host, linkedHosting);
    instance.startObserver();
    if(host){
      _removeHostEntry();
      _hostingController.instance.value = instance;
      _hostingController.saveInstance();
    }else{
      _gameController.instance.value = instance;
      _gameController.saveInstance();
    }
    _injectOrShowError(Injectable.sslBypass, host);
  }

  Future<bool> _startMatchMakingServer() async {
    if(widget.host){
      return false;
    }

    var matchmakingIp = _matchmakerController.gameServerAddress.text;
    if(!isLocalHost(matchmakingIp)) {
      return false;
    }

    if(!_gameController.autoStartGameServer()){
      return false;
    }

    if(_hostingController.started()){
      return false;
    }

    var version = _gameController.selectedVersion!;
    await _startGameProcesses(version, true, false);
    return true;
  }

  Future<int> _createGameProcess(String gamePath, bool host) async {
    var gameArgs = createRebootArgs(_safeUsername, _gameController.password.text, host, _gameController.customLaunchArgs.text);
    var gameProcess = await Process.start(gamePath, gameArgs);
    gameProcess
      ..exitCode.then((_) => _onEnd())
      ..outLines.forEach((line) => _onGameOutput(line, host))
      ..errLines.forEach((line) => _onGameOutput(line, host));
    return gameProcess.pid;
  }

  String get _safeUsername {
    if (_gameController.username.text.isEmpty) {
      return kDefaultPlayerName;
    }

    var username = _gameController.username.text;
    if(_gameController.password.text.isNotEmpty){
      return username;
    }

    username = _gameController.username.text.replaceAll(RegExp("[^A-Za-z0-9]"), "").trim();
    if(username.isEmpty){
      return kDefaultPlayerName;
    }

    return username;
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

  void _onEnd() {
    if(_fail){
      return;
    }

    _onStop(widget.host, false);
  }

  void _closeLaunchingWidget(bool host, bool message) async {
    if(!message) {
      return;
    }

    if(_fail) {
      showInfoBar(
          "An error occurred while starting the headless server",
          severity: InfoBarSeverity.error
      );
      return;
    }

    var theme = FluentTheme.of(context);
    showInfoBar(
        "Waiting for the game server to boot up...",
        loading: true,
        duration: null
    );
    var gameServerPort = _settingsController.gameServerPort.text;
    var localPingResult = await pingGameServer(
        "localhost:$gameServerPort",
        timeout: const Duration(minutes: 1)
    );
    if(!localPingResult) {
      showInfoBar(
          "The headless server was started successfully, but the game server didn't boot",
          severity: InfoBarSeverity.error,
          duration: snackbarLongDuration
      );
      return;
    }

    _matchmakerController.joinLocalHost();
    var accessible = await _checkAccessible(theme, gameServerPort);
    if(!accessible) {
      showInfoBar(
          "The game server was started successfully, but other players can't join",
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
        "The game server was started successfully",
        severity: InfoBarSeverity.success,
        duration: snackbarLongDuration
    );
  }

  Future<bool> _checkAccessible(FluentThemeData theme, String gameServerPort) async {
    showInfoBar(
        "Checking if other players can join the game server...",
        loading: true,
        duration: null
    );
    var publicIp = await Ipify.ipv4();
    var externalResult = await pingGameServer("$publicIp:$gameServerPort");
    if(externalResult) {
      return true;
    }

    var future = CancelableOperation.fromFuture(pingGameServer(
        "$publicIp:$gameServerPort",
        timeout: const Duration(days: 365)
    ));
    showInfoBar(
        Text.rich(
            TextSpan(
                children: [
                  const TextSpan(
                      text: "Other players can't join the game server currently: please follow "
                  ),
                  TextSpan(
                      text: "this tutorial",
                      mouseCursor: SystemMouseCursors.click,
                      style: TextStyle(
                          color: theme.accentColor.dark
                      ),
                      recognizer: TapGestureRecognizer()..onTap = openPortTutorial
                  ),
                  const TextSpan(
                      text: " to fix this problem"
                  ),
                ]
            )
        ),
        action: Button(
          onPressed: () {
            future.cancel();
            removeMessage(1);
          },
          child: const Text("Ignore"),
        ),
        severity: InfoBarSeverity.warning,
        duration: null,
        loading: true
    );
    return await future.valueOrCancellation() ?? false;
  }

  void _onGameOutput(String line, bool host) {
    _logFile.createSync(recursive: true);
    _logFile.writeAsString("$line\n", mode: FileMode.append);
    if (line.contains(shutdownLine)) {
      _onStop(host, false);
      return;
    }

    if(corruptedBuildErrors.any((element) => line.contains(element))){
      if(_fail){
        return;
      }

      _fail = true;
      showCorruptedBuildError(host);
      _onStop(host, false);
      return;
    }

    if(cannotConnectErrors.any((element) => line.contains(element))){
      if(_fail){
        return;
      }

      _showTokenError(host);
      return;
    }

    if(line.contains("Region ")){
      if(!host){
        _injectOrShowError(Injectable.console, host);
      }else {
        _injectOrShowError(Injectable.reboot, host)
            .then((value) => _closeLaunchingWidget(host, true));
      }

      _injectOrShowError(Injectable.memoryFix, host);
      var instance = host ? _hostingController.instance.value : _gameController.instance.value;
      instance?.tokenError = false;
    }
  }

  Future<void> _showTokenError(bool host) async {
    _fail = true;
    var instance = host ? _hostingController.instance.value : _gameController.instance.value;
    if(_authenticatorController.type() != ServerType.embedded) {
      showTokenErrorUnfixable();
      instance?.tokenError = true;
      return;
    }

    await _authenticatorController.restartInteractive();
    showTokenErrorFixable();
    _onStop(host, false);
    _start();
  }

  void _onStop(bool host, bool showMessage) async {
    var instance = host ? _hostingController.instance.value : _gameController.instance.value;
    if(instance != null){
      if(instance.linkedHosting){
        _onStop(true, showMessage);
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
      await _removeHostEntry();
    }

    _closeLaunchingWidget(host, showMessage);
  }

  Future<void> _removeHostEntry() async {
    await _supabase.from('hosts')
        .delete()
        .match({'id': _hostingController.uuid});
  }

  Future<void> _injectOrShowError(Injectable injectable, bool hosting) async {
    var instance = hosting ? _hostingController.instance.value : _gameController.instance.value;
    if (instance == null) {
      return;
    }

    try {
      var gameProcess = instance.gamePid;
      var dllPath = await _getDllPath(injectable, hosting);
      if(dllPath == null) {
        return;
      }

      await injectDll(gameProcess, dllPath.path);
    } catch (exception) {
      showInfoBar("Cannot inject $injectable.dll: $exception");
      _onStop(hosting, false);
    }
  }

  Future<File?> _getDllPath(Injectable injectable, bool hosting) async {
    Future<File> getPath(Injectable injectable) async {
      switch(injectable){
        case Injectable.reboot:
          return File(_settingsController.gameServerDll.text);
        case Injectable.console:
          return File(_settingsController.unrealEngineConsoleDll.text);
        case Injectable.sslBypass:
          return File(_settingsController.authenticatorDll.text);
        case Injectable.memoryFix:
          return File("${assetsDirectory.path}\\dlls\\memoryleak.dll");
      }
    }

    var dllPath = await getPath(injectable);
    if(dllPath.existsSync()) {
      return dllPath;
    }

    _onDllFail(dllPath, hosting);
    return null;
  }

  void _onDllFail(File dllPath, bool hosting) {
    WidgetsBinding.instance.addPostFrameCallback((_) {
      _fail = true;
      showMissingDllError(path.basename(dllPath.path));
      _onStop(hosting, false);
    });
  }
}

enum Injectable {
  console,
  sslBypass,
  reboot,
  memoryFix
}
