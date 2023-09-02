import 'dart:async';
import 'dart:io';

import 'package:dart_ipify/dart_ipify.dart';
import 'package:fluent_ui/fluent_ui.dart' hide showDialog;
import 'package:get/get.dart';
import 'package:path/path.dart' as path;
import 'package:process_run/shell.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/hosting_controller.dart';
import 'package:reboot_launcher/src/controller/authenticator_controller.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/interactive/game.dart';
import 'package:reboot_launcher/src/interactive/server.dart';
import 'package:reboot_launcher/src/dialog/message.dart';
import 'package:reboot_launcher/src/util/cryptography.dart';
import 'package:reboot_launcher/src/util/watch.dart';
import 'package:supabase_flutter/supabase_flutter.dart';

class LaunchButton extends StatefulWidget {
  final bool host;
  final String? startLabel;
  final String? stopLabel;
  final bool Function()? onTap;

  const LaunchButton({Key? key, required this.host, this.startLabel, this.stopLabel, this.onTap}) : super(key: key);

  @override
  State<LaunchButton> createState() => _LaunchButtonState();
}

class _LaunchButtonState extends State<LaunchButton> {
  final GameController _gameController = Get.find<GameController>();
  final HostingController _hostingController = Get.find<HostingController>();
  final AuthenticatorController _authenticatorController = Get.find<AuthenticatorController>();
  final SettingsController _settingsController = Get.find<SettingsController>();
  final File _logFile = File("${logsDirectory.path}\\game.log");
  Completer<bool> _completer = Completer();
  bool _fail = false;
  Future? _executor;

  @override
  Widget build(BuildContext context) => Align(
    alignment: AlignmentDirectional.bottomCenter,
    child: SizedBox(
      width: double.infinity,
      child: Obx(() => SizedBox(
        height: 48,
        child: Button(
          child: Align(
            alignment: Alignment.center,
            child: Text(_hasStarted ? _stopMessage : _startMessage)
          ),
          onPressed: () => _executor = _start()
        ),
      )),
    ),
  );

  bool get _hasStarted => widget.host ? _hostingController.started() : _gameController.started();

  void _setStarted(bool hosting, bool started) => hosting ? _hostingController.started.value = started : _gameController.started.value = started;

  String get _startMessage => widget.startLabel ?? (widget.host ? "Start hosting" : "Launch fortnite");

  String get _stopMessage => widget.stopLabel ?? (widget.host ? "Stop hosting" : "Close fortnite");

  Future<void> _start() async {
    if(widget.onTap != null && !widget.onTap!()){
      return;
    }

    if (_hasStarted) {
      _onStop(widget.host);
      return;
    }

    if(_gameController.selectedVersion == null){
      showMessage("Select a Fortnite version before continuing");
      _onStop(widget.host);
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
        _onStop(widget.host);
        return;
      }

      var result = _authenticatorController.started() || await _authenticatorController.toggleInteractive(false);
      if(!result){
        _onStop(widget.host);
        return;
      }

      var automaticallyStartedServer = await _startMatchMakingServer();
      await _startGameProcesses(version, widget.host, automaticallyStartedServer);
      if(widget.host){
        await _showServerLaunchingWarning();
      }
    } catch (exception, stacktrace) {
      _closeLaunchingWidget(false);
      _onStop(widget.host);
      showCorruptedBuildError(widget.host, exception, stacktrace);
    }
  }

  Future<void> _startGameProcesses(FortniteVersion version, bool host, bool linkedHosting) async {
    _setStarted(host, true);
    var launcherProcess = await _createLauncherProcess(version);
    var eacProcess = await _createEacProcess(version);
    var executable = await version.executable;
    if(executable == null){
      showMissingBuildError(version);
      _onStop(widget.host);
      return;
    }

    var gameProcess = await _createGameProcess(executable.path, host);
    var instance = GameInstance(gameProcess, launcherProcess, eacProcess, host, linkedHosting);
    instance.startObserver();
    if(host){
      _hostingController.instance.value = instance;
    }else{
      _gameController.instance.value = instance;
    }
    _injectOrShowError(Injectable.sslBypass, host);
  }

  Future<bool> _startMatchMakingServer() async {
    if(widget.host){
      return false;
    }

    // var matchmakingIp = _settingsController.matchmakingIp.text;
    var matchmakingIp = "127.0.0.1";
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

    _closeLaunchingWidget(false);
    _onStop(widget.host);
  }

  void _closeLaunchingWidget(bool success) {
    showMessage(
        success ? "The headless server was started successfully" : "An error occurred while starting the headless server",
        severity: success ? InfoBarSeverity.success : InfoBarSeverity.error
    );
    if(!_completer.isCompleted) {
      _completer.complete(success);
    }
  }

  Future<void> _showServerLaunchingWarning() async {
    showMessage(
        "Launching headless server...",
        loading: true,
        duration: null
    );
    var result = await _completer.future;
    if(!result){
      _onStop(true);
      return;
    }

    if(!_hostingController.discoverable.value){
      return;
    }

    var password = _hostingController.password.text;
    var hasPassword = password.isNotEmpty;
    var ip = await Ipify.ipv4();
    if(hasPassword) {
      ip = aes256Encrypt(ip, password);
    }

    var supabase = Supabase.instance.client;
    await supabase.from('hosts').insert({
      'id': _gameController.uuid,
      'name': _hostingController.name.text,
      'description': _hostingController.description.text,
      'author': _gameController.username.text,
      'ip': ip,
      'version': _gameController.selectedVersion?.name,
      'password': hasPassword ? hashPassword(password) : null,
      'timestamp': DateTime.now().toIso8601String(),
      'discoverable': _hostingController.discoverable.value
    });
  }

  void _onGameOutput(String line, bool host) {
    _logFile.createSync(recursive: true);
    _logFile.writeAsString("$line\n", mode: FileMode.append);
    if (line.contains(shutdownLine)) {
      _onStop(host);
      return;
    }

    if(corruptedBuildErrors.any((element) => line.contains(element))){
      if(_fail){
        return;
      }

      _fail = true;
      showCorruptedBuildError(host);
      _onStop(host);
      return;
    }

    if(cannotConnectErrors.any((element) => line.contains(element))){
      if(_fail){
        return;
      }

      _fail = true;
      _closeLaunchingWidget(false);
      _showTokenError(host);
      return;
    }

    if(line.contains("Region ")){
      if(!host){
        _injectOrShowError(Injectable.console, host);
      }else {
        _injectOrShowError(Injectable.reboot, host)
            .then((value) => _closeLaunchingWidget(true));
      }

      _injectOrShowError(Injectable.memoryFix, host);
      var instance = host ? _hostingController.instance.value : _gameController.instance.value;
      instance?.tokenError = false;
    }
  }

  Future<void> _showTokenError(bool host) async {
    var instance = host ? _hostingController.instance.value : _gameController.instance.value;
    if(_authenticatorController.type() != ServerType.embedded) {
      showTokenErrorUnfixable();
      instance?.tokenError = true;
      return;
    }

    await _authenticatorController.restartInteractive();
    showTokenErrorFixable();
    _onStop(host);
    _start();
  }

  void _onStop(bool host) async {
    if(_executor != null){
      await _executor;
    }

    var instance = host ? _hostingController.instance.value : _gameController.instance.value;
    if(instance != null){
      if(instance.linkedHosting){
        _onStop(true);
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
      var supabase = Supabase.instance.client;
      await supabase.from('hosts')
          .delete()
          .match({'id': _gameController.uuid});
    }

    _completer = Completer();
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
      showMessage("Cannot inject $injectable.dll: $exception");
      _onStop(hosting);
    }
  }

  Future<File?> _getDllPath(Injectable injectable, bool hosting) async {
    Future<File> getPath(Injectable injectable) async {
      switch(injectable){
        case Injectable.reboot:
          return File(_settingsController.rebootDll.text);
        case Injectable.console:
          return File(_settingsController.consoleDll.text);
        case Injectable.sslBypass:
          return File(_settingsController.authDll.text);
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
      _closeLaunchingWidget(false);
      showMissingDllError(path.basename(dllPath.path));
      _onStop(hosting);
    });
  }
}

enum Injectable {
  console,
  sslBypass,
  reboot,
  memoryFix
}
