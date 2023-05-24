import 'dart:async';
import 'dart:collection';
import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:get/get.dart';
import 'package:process_run/shell.dart';
import 'package:reboot_launcher/src/ui/controller/game_controller.dart';
import 'package:reboot_launcher/src/ui/controller/hosting_controller.dart';
import 'package:reboot_launcher/src/ui/controller/server_controller.dart';
import 'package:reboot_launcher/src/ui/dialog/dialog.dart';
import 'package:reboot_launcher/src/ui/dialog/game_dialogs.dart';
import 'package:reboot_launcher/src/ui/dialog/server_dialogs.dart';
import 'package:reboot_launcher/src/model/fortnite_version.dart';
import 'package:reboot_launcher/src/model/server_type.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/util/injector.dart';
import 'package:reboot_launcher/src/util/patcher.dart';
import 'package:reboot_launcher/src/util/server.dart';
import 'package:path/path.dart' as path;

import 'package:reboot_launcher/src/../main.dart';
import 'package:reboot_launcher/src/ui/controller/settings_controller.dart';
import 'package:reboot_launcher/src/ui/dialog/snackbar.dart';
import 'package:reboot_launcher/src/model/game_instance.dart';

import '../../../util/process.dart';

class LaunchButton extends StatefulWidget {
  final bool host;

  const LaunchButton({Key? key, required this.host}) : super(key: key);

  @override
  State<LaunchButton> createState() => _LaunchButtonState();
}

class _LaunchButtonState extends State<LaunchButton> {
  final String _shutdownLine = "FOnlineSubsystemGoogleCommon::Shutdown()";
  final List<String> _corruptedBuildErrors = [
    "when 0 bytes remain",
    "Pak chunk signature verification failed!"
  ];
  final List<String> _errorStrings = [
    "port 3551 failed: Connection refused",
    "Unable to login to Fortnite servers",
    "HTTP 400 response from ",
    "Network failure when attempting to check platform restrictions",
    "UOnlineAccountCommon::ForceLogout"
  ];

  final GameController _gameController = Get.find<GameController>();
  final HostingController _hostingController = Get.find<HostingController>();
  final ServerController _serverController = Get.find<ServerController>();
  final SettingsController _settingsController = Get.find<SettingsController>();
  final File _logFile = File("${assetsDirectory.path}\\logs\\game.log");
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
            child: Text(
                _hasStarted ? _stopMessage : _startMessage
            ),
          ),
          onPressed: () => _executor = _start()
        ),
      )),
    ),
  );

  bool get _hasStarted => widget.host ? _hostingController.started() : _gameController.started();

  void _setStarted(bool hosting, bool started) => hosting ? _hostingController.started.value = started : _gameController.started.value = started;

  String get _startMessage => widget.host ? "Start hosting" : "Launch fortnite";

  String get _stopMessage => widget.host ? "Stop hosting" : "Close fortnite";

  Future<void> _start() async {
    if (_hasStarted) {
      _onStop(widget.host);
      return;
    }

    _setStarted(widget.host, true);
    if (_gameController.username.text.isEmpty) {
      if(_serverController.type() != ServerType.local){
        showMessage("Missing username");
        _onStop(widget.host);
        return;
      }

      showMessage("No username: expecting self sign in");
    }

    if (_gameController.selectedVersion == null) {
      showMessage("No version is selected");
      _onStop(widget.host);
      return;
    }

    for (var element in Injectable.values) {
      if(await _getDllPath(element, widget.host) == null) {
        return;
      }
    }

    try {
      _fail = false;
      var version = _gameController.selectedVersion!;
      var gamePath = version.executable?.path;
      if(gamePath == null){
        showMissingBuildError(version);
        _onStop(widget.host);
        return;
      }

      var result = _serverController.started() || await _serverController.toggle(true);
      if(!result){
        _onStop(widget.host);
        return;
      }

      await compute(patchMatchmaking, version.executable!);
      await compute(patchHeadless, version.executable!);

      var automaticallyStartedServer = await _startMatchMakingServer();
      await _startGameProcesses(version, widget.host, automaticallyStartedServer);

      if(widget.host){
        await _showServerLaunchingWarning();
      }
    } catch (exception, stacktrace) {
      _closeDialogIfOpen(false);
      showCorruptedBuildError(widget.host, exception, stacktrace);
      _onStop(widget.host);
    }
  }

  Future<void> _startGameProcesses(FortniteVersion version, bool host, bool hasChildServer) async {
    _setStarted(host, true);
    var launcherProcess = await _createLauncherProcess(version);
    var eacProcess = await _createEacProcess(version);
    var gameProcess = await _createGameProcess(version.executable!.path, host);
    var instance = GameInstance(gameProcess, launcherProcess, eacProcess, hasChildServer);
    if(host){
      _hostingController.instance = instance;
    }else{
      _gameController.instance = instance;
    }
    _injectOrShowError(Injectable.sslBypass, host);
  }

  Future<bool> _startMatchMakingServer() async {
    if(widget.host){
      return false;
    }

    var matchmakingIp = _settingsController.matchmakingIp.text;
    if(!matchmakingIp.contains("127.0.0.1") && !matchmakingIp.contains("localhost")) {
      return false;
    }

    var version = _gameController.selectedVersion!;
    await _startGameProcesses(version, true, false);
    return true;
  }

  Future<Process> _createGameProcess(String gamePath, bool host) async {
    var gameArgs = createRebootArgs(_safeUsername, host, _gameController.customLaunchArgs.text);
    var gameProcess = await Process.start(gamePath, gameArgs);
    gameProcess
      ..exitCode.then((_) => _onEnd())
      ..outLines.forEach((line) => _onGameOutput(line, host))
      ..errLines.forEach((line) => _onGameOutput(line, host));
    return gameProcess;
  }

  String get _safeUsername {
    if (_gameController.username.text.isEmpty) {
      return kDefaultPlayerName;
    }

    var username = _gameController.username.text.replaceAll(RegExp("[^A-Za-z0-9]"), "").trim();
    if(username.isEmpty){
      return kDefaultPlayerName;
    }

    return username;
  }

  Future<Process?> _createLauncherProcess(FortniteVersion version) async {
    var launcherFile = version.launcher;
    if (launcherFile == null) {
      return null;
    }

    var launcherProcess = await Process.start(launcherFile.path, []);
    suspend(launcherProcess.pid);
    return launcherProcess;
  }

  Future<Process?> _createEacProcess(FortniteVersion version) async {
    var eacFile = version.eacExecutable;
    if (eacFile == null) {
      return null;
    }

    var eacProcess = await Process.start(eacFile.path, []);
    suspend(eacProcess.pid);
    return eacProcess;
  }

  void _onEnd() {
    if(_fail){
      return;
    }

    _closeDialogIfOpen(false);
    _onStop(widget.host);
  }

  void _closeDialogIfOpen(bool success) {
    var route = ModalRoute.of(appKey.currentContext!);
    if(route == null || route.isCurrent){
      return;
    }

    Navigator.of(appKey.currentContext!).pop(success);
  }

  Future<void> _showServerLaunchingWarning() async {
    var result = await showDialog<bool>(
        context: appKey.currentContext!,
        builder: (context) => ProgressDialog(
            text: "Launching headless server...",
            onStop: () =>_onEnd()
        )
    ) ?? false;

    if(result){
      return;
    }

    _onStop(widget.host);
  }

  void _onGameOutput(String line, bool host) {
    _logFile.createSync(recursive: true);
    _logFile.writeAsString("$line\n", mode: FileMode.append);
    if (line.contains(_shutdownLine)) {
      _onStop(host);
      return;
    }

    if(_corruptedBuildErrors.any((element) => line.contains(element))){
      if(_fail){
        return;
      }

      _fail = true;
      showCorruptedBuildError(host);
      _onStop(host);
      return;
    }

    if(_errorStrings.any((element) => line.contains(element))){
      if(_fail){
        return;
      }

      _fail = true;
      _closeDialogIfOpen(false);
      _showTokenError(host);
      return;
    }

    if(line.contains("Region ")){
      if(!host){
        _injectOrShowError(Injectable.console, host);
      }else {
        _injectOrShowError(Injectable.reboot, host)
            .then((value) => _closeDialogIfOpen(true));
      }

      _injectOrShowError(Injectable.memoryFix, host);
      var instance = host ? _hostingController.instance : _gameController.instance;
      instance?.tokenError = false;
    }
  }

  Future<void> _showTokenError(bool host) async {
    var instance = host ? _hostingController.instance : _gameController.instance;
    if(_serverController.type() != ServerType.embedded) {
      showTokenErrorUnfixable();
      instance?.tokenError = true;
      return;
    }

    var tokenError = instance?.tokenError;
    instance?.tokenError = true;
    await _serverController.restart(true);
    if (tokenError == true) {
      showTokenErrorCouldNotFix();
      return;
    }

    showTokenErrorFixable();
    _onStop(host);
    _start();
  }

  void _onStop(bool host) async {
    if(_executor != null){
      await _executor;
    }

    var instance = host ? _hostingController.instance : _gameController.instance;
    if(instance != null){
      if(instance.hasChildServer){
        _onStop(true);
      }

      instance.kill();
      if(host){
        _hostingController.instance = null;
      }else {
        _gameController.instance = null;
      }
    }

    _setStarted(host, false);
  }

  Future<void> _injectOrShowError(Injectable injectable, bool hosting) async {
    var instance = hosting ? _hostingController.instance : _gameController.instance;
    if (instance == null) {
      return;
    }

    try {
      var gameProcess = instance.gameProcess;
      var dllPath = await _getDllPath(injectable, hosting);
      if(dllPath == null) {
        return;
      }

      await injectDll(gameProcess.pid, dllPath.path);
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
      if(_fail){
        return;
      }

      _fail = true;
      _closeDialogIfOpen(false);
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
