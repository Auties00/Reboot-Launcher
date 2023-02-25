import 'dart:async';
import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:get/get.dart';
import 'package:process_run/shell.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';
import 'package:reboot_launcher/src/dialog/dialog.dart';
import 'package:reboot_launcher/src/dialog/game_dialogs.dart';
import 'package:reboot_launcher/src/dialog/server_dialogs.dart';
import 'package:reboot_launcher/src/model/fortnite_version.dart';
import 'package:reboot_launcher/src/model/game_type.dart';
import 'package:reboot_launcher/src/model/server_type.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/util/injector.dart';
import 'package:reboot_launcher/src/util/patcher.dart';
import 'package:reboot_launcher/src/util/reboot.dart';
import 'package:reboot_launcher/src/util/server.dart';
import 'package:path/path.dart' as path;

import 'package:reboot_launcher/src/../main.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/dialog/snackbar.dart';
import 'package:reboot_launcher/src/model/game_instance.dart';

import '../../util/process.dart';

class LaunchButton extends StatefulWidget {
  const LaunchButton(
      {Key? key})
      : super(key: key);

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
  final ServerController _serverController = Get.find<ServerController>();
  final SettingsController _settingsController = Get.find<SettingsController>();
  File? _logFile;
  bool _fail = false;

  @override
  void initState() {
    loadBinary("game.txt", true)
        .then((value) => _logFile = value);
    super.initState();
  }

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
                _gameController.started() ? "Close fortnite" : "Launch fortnite"
            ),
          ),
          onPressed: () => _start(_gameController.type()),
        ),
      )),
    ),
  );

  void _start(GameType type) async {
    if (_gameController.started()) {
      _onStop(type);
      return;
    }

    _gameController.started.value = true;
    if (_gameController.username.text.isEmpty) {
      if(_serverController.type() != ServerType.local){
        showMessage("Missing username");
        _onStop(type);
        return;
      }

      showMessage("No username: expecting self sign in");
    }

    if (_gameController.selectedVersion == null) {
      showMessage("No version is selected");
      _onStop(type);
      return;
    }

    for (var element in Injectable.values) {
      if(await _getDllPath(element, type) == null) {
        return;
      }
    }

    try {
      _fail = false;
      await _resetLogFile();

      var version = _gameController.selectedVersion!;
      var gamePath = version.executable?.path;
      if(gamePath == null){
        showMissingBuildError(version);
        _onStop(type);
        return;
      }

      var result = _serverController.started() || await _serverController.toggle();
      if(!result){
        _onStop(type);
        return;
      }

      await compute(patchMatchmaking, version.executable!);
      await compute(patchHeadless, version.executable!);

      var automaticallyStartedServer = await _startMatchMakingServer();
      await _startGameProcesses(version, type, automaticallyStartedServer);

      if(type == GameType.headlessServer){
        await _showServerLaunchingWarning();
      }
    } catch (exception, stacktrace) {
      _closeDialogIfOpen(false);
      showCorruptedBuildError(type != GameType.client, exception, stacktrace);
      _onStop(type);
    }
  }

  Future<void> _startGameProcesses(FortniteVersion version, GameType type, bool hasChildServer) async {
    var launcherProcess = await _createLauncherProcess(version);
    var eacProcess = await _createEacProcess(version);
    var gameProcess = await _createGameProcess(version.executable!.path, type);
    _gameController.gameInstancesMap[type] = GameInstance(gameProcess, launcherProcess, eacProcess, hasChildServer);
    _injectOrShowError(Injectable.cranium, type);
  }

  Future<bool> _startMatchMakingServer() async {
    if(_gameController.type() != GameType.client){
      return false;
    }

    var matchmakingIp = _settingsController.matchmakingIp.text;
    if(!matchmakingIp.contains("127.0.0.1") && !matchmakingIp.contains("localhost")) {
      return false;
    }

    if(!_gameController.autostartGameServer()){
      return false;
    }

    var version = _gameController.selectedVersion!;
    await _startGameProcesses(
        version,
        GameType.headlessServer,
        false
    );
    return true;
  }

  Future<Process> _createGameProcess(String gamePath, GameType type) async {
    var gameArgs = createRebootArgs(_gameController.username.text, type, _gameController.customLaunchArgs.text);
    var gameProcess = await Process.start(gamePath, gameArgs);
    gameProcess
      ..exitCode.then((_) => _onEnd(type))
      ..outLines.forEach((line) => _onGameOutput(line, type))
      ..errLines.forEach((line) => _onGameOutput(line, type));
    return gameProcess;
  }

  Future<void> _resetLogFile() async {
    if(_logFile != null && await _logFile!.exists()){
      await _logFile!.delete();
    }
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

  void _onEnd(GameType type) {
    if(_fail){
      return;
    }

    _closeDialogIfOpen(false);
    _onStop(type);
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
            onStop: () =>_onEnd(_gameController.type())
        )
    ) ?? false;

    if(result){
      return;
    }

    _onStop(_gameController.type());
  }

  void _onGameOutput(String line, GameType type) {
    if(_logFile != null){
      _logFile!.writeAsString("$line\n", mode: FileMode.append);
    }

    if (line.contains(_shutdownLine)) {
      _onStop(type);
      return;
    }

    if(_corruptedBuildErrors.any((element) => line.contains(element))){
      if(_fail){
        return;
      }

      _fail = true;
      showCorruptedBuildError(type != GameType.client);
      _onStop(type);
      return;
    }

    if(_errorStrings.any((element) => line.contains(element))){
      if(_fail){
        return;
      }

      _fail = true;
      _closeDialogIfOpen(false);
      _showTokenError(type);
      return;
    }

    if(line.contains("Region ")){
      if(type == GameType.client){
        _injectOrShowError(Injectable.console, type);
      }else {
        _injectOrShowError(Injectable.reboot, type)
            .then((value) => _closeDialogIfOpen(true));
      }

      _injectOrShowError(Injectable.memoryFix, type);
      _gameController.currentGameInstance?.tokenError = false;
    }
  }

  Future<void> _showTokenError(GameType type) async {
    if(_serverController.type() != ServerType.embedded) {
      showTokenErrorUnfixable();
      _gameController.currentGameInstance?.tokenError = true;
      return;
    }

    var tokenError = _gameController.currentGameInstance?.tokenError;
    _gameController.currentGameInstance?.tokenError = true;
    await _serverController.restart();
    if (tokenError == true) {
      showTokenErrorCouldNotFix();
      return;
    }

    showTokenErrorFixable();
    _onStop(type);
    _start(type);
  }

  void _onStop(GameType? type) {
    if(type == null){
      return;
    }

    var value = _gameController.gameInstancesMap[type];
    if(value != null){
      if(value.hasChildServer){
        _onStop(GameType.headlessServer);
      }

      value.kill();
      _gameController.gameInstancesMap.remove(type);
    }

    if(type == _gameController.type()) {
      _gameController.started.value = false;
    }
  }

  Future<void> _injectOrShowError(Injectable injectable, GameType type) async {
    var gameProcess = _gameController.gameInstancesMap[type]?.gameProcess;
    if (gameProcess == null) {
      return;
    }

    try {
      var dllPath = await _getDllPath(injectable, type);
      if(dllPath == null) {
        return;
      }

      await injectDll(gameProcess.pid, dllPath.path);
    } catch (exception) {
      showMessage("Cannot inject $injectable.dll: $exception");
      _onStop(type);
    }
  }

  Future<File?> _getDllPath(Injectable injectable, GameType type) async {
    Future<File> getPath(Injectable injectable) async {
      switch(injectable){
        case Injectable.reboot:
          return File(_settingsController.rebootDll.text);
        case Injectable.console:
          return File(_settingsController.consoleDll.text);
        case Injectable.cranium:
          return File(_settingsController.authDll.text);
        case Injectable.memoryFix:
          return await loadBinary("leakv2.dll", true);
      }
    }

    var dllPath = await getPath(injectable);
    if(dllPath.existsSync()) {
      return dllPath;
    }

    await _downloadMissingDll(injectable);
    if(dllPath.existsSync()) {
      return dllPath;
    }

    _onDllFail(dllPath, type);
    return null;
  }

  void _onDllFail(File dllPath, GameType type) {
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if(_fail){
        return;
      }

      _fail = true;
      _closeDialogIfOpen(false);
      showMissingDllError(path.basename(dllPath.path));
      _onStop(type);
    });
  }

  Future<void> _downloadMissingDll(Injectable injectable) async {
    if(injectable != Injectable.reboot){
      await loadBinary("$injectable.dll", true);
      return;
    }

    await downloadRebootDll(rebootDownloadUrl, 0);
  }
}

enum Injectable {
  console,
  cranium,
  reboot,
  memoryFix
}
