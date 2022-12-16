import 'dart:async';
import 'dart:io';

import 'package:async/async.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:get/get.dart';
import 'package:process_run/shell.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';
import 'package:reboot_launcher/src/dialog/dialog.dart';
import 'package:reboot_launcher/src/dialog/game_dialogs.dart';
import 'package:reboot_launcher/src/dialog/server_dialogs.dart';
import 'package:reboot_launcher/src/model/game_type.dart';
import 'package:reboot_launcher/src/model/server_type.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/util/injector.dart';
import 'package:reboot_launcher/src/util/patcher.dart';
import 'package:reboot_launcher/src/util/reboot.dart';
import 'package:reboot_launcher/src/util/server.dart';
import 'package:win32_suspend_process/win32_suspend_process.dart';
import 'package:path/path.dart' as path;

import '../../../main.dart';
import '../../controller/settings_controller.dart';
import '../../dialog/snackbar.dart';

class LaunchButton extends StatefulWidget {
  const LaunchButton(
      {Key? key})
      : super(key: key);

  @override
  State<LaunchButton> createState() => _LaunchButtonState();
}

class _LaunchButtonState extends State<LaunchButton> {
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
  Widget build(BuildContext context) {
    return Align(
      alignment: AlignmentDirectional.bottomCenter,
      child: SizedBox(
        width: double.infinity,
        child: Obx(() => Tooltip(
          message: _gameController.started() ? "Close the running Fortnite instance" : "Launch a new Fortnite instance",
          child: Button(
              onPressed: _onPressed,
              child: Text(_gameController.started() ? "Close" : "Launch")
          ),
        )),
      ),
    );
  }

  void _onPressed() async {
    if (_gameController.started()) {
      _onStop();
      return;
    }

    if (_gameController.username.text.isEmpty && _gameController.type() != GameType.client) {
      showMessage("Missing username");
      _gameController.started.value = false;
      return;
    }

    _gameController.started.value = true;
    if (_gameController.selectedVersionObs.value == null) {
      showMessage("No version is selected");
      _gameController.started.value = false;
      return;
    }

    try {
      var version = _gameController.selectedVersionObs.value!;
      var gamePath = version.executable?.path;
      if(gamePath == null){
        _onError("${version.location.path} no longer contains a Fortnite executable, did you delete or move it?", null);
        _onStop();
        return;
      }

      if (version.launcher != null) {
        _gameController.launcherProcess = await Process.start(version.launcher!.path, []);
        Win32Process(_gameController.launcherProcess!.pid).suspend();
      }

      if (version.eacExecutable != null) {
        _gameController.eacProcess = await Process.start(version.eacExecutable!.path, []);
        Win32Process(_gameController.eacProcess!.pid).suspend();
      }

      var result = await _serverController.start(
          required: true,
          askPortKill: false,
      );
      if(!result){
        showMessage("Cannot launch the game as the backend didn't start up correctly");
        _onStop();
        return;
      }

      if(_logFile != null && await _logFile!.exists()){
        await _logFile!.delete();
      }

      await compute(patchMatchmaking, version.executable!);
      await compute(patchHeadless, version.executable!);

      var headlessHosting = _gameController.type() == GameType.headlessServer;
      var arguments = createRebootArgs(_gameController.username.text, _gameController.type.value);
      _gameController.gameProcess = await Process.start(gamePath, arguments)
        ..exitCode.then((_) => _onEnd())
        ..outLines.forEach((line) => _onGameOutput(line))
        ..errLines.forEach((line) => _onGameOutput(line));
      _injectOrShowError(Injectable.cranium);
      if(headlessHosting){
        await _showServerLaunchingWarning();
      }
    } catch (exception, stacktrace) {
      _closeDialogIfOpen(false);
      _onError(exception, stacktrace);
      _onStop();
    }
  }

  void _onEnd() {
    if(_fail){
      return;
    }

    _closeDialogIfOpen(false);
    _onStop();
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
          onStop: () {
            Navigator.of(context).pop(false);
            _onStop();
          }
        )
    );

    if(result != null && result){
      return;
    }

    _onStop();
  }

  void _onGameOutput(String line) {
    if(_logFile != null){
      _logFile!.writeAsString("$line\n", mode: FileMode.append);
    }

    if (line.contains("FOnlineSubsystemGoogleCommon::Shutdown()")) {
      _onStop();
      return;
    }

    if(_errorStrings.any((element) => line.contains(element))){
      if(_fail){
        return;
      }

      _fail = true;
      _closeDialogIfOpen(false);
      _showTokenError();
      return;
    }

    if(line.contains("Region ")){
      if(_gameController.type.value == GameType.client){
        _injectOrShowError(Injectable.console);
      }else {
        _injectOrShowError(Injectable.reboot)
            .then((value) => _closeDialogIfOpen(true));
      }

      _injectOrShowError(Injectable.memoryFix);
    }
  }

  Future<void> _showTokenError() async {
    if(_serverController.type() == ServerType.embedded) {
      showTokenErrorFixable();
      await _serverController.start(
        required: true,
        askPortKill: false
      );
    } else {
      showTokenErrorUnfixable();
    }
  }

  Future<Object?> _onError(Object exception, StackTrace? stackTrace) async {
    return showDialog(
        context: context,
        builder: (context) => ErrorDialog(
          exception: exception,
          stackTrace: stackTrace,
          errorMessageBuilder: (exception) => "Cannot launch fortnite: $exception"
        )
    );
  }

  void _onStop() {
    _gameController.started.value = false;
    _gameController.kill();
  }

  Future<void> _injectOrShowError(Injectable injectable) async {
    var gameProcess = _gameController.gameProcess;
    if (gameProcess == null) {
      return;
    }

    try {
      var dllPath = await _getDllPath(injectable);
      if(!dllPath.existsSync()) {
        await _downloadMissingDll(injectable);
        if(!dllPath.existsSync()){
          _onDllFail(dllPath);
          return;
        }
      }

      await injectDll(gameProcess.pid, dllPath.path);
    } catch (exception) {
      showSnackbar(
          appKey.currentContext!,
          Snackbar(
              content: Text("Cannot inject $injectable.dll: $exception", textAlign: TextAlign.center),
              extended: true
          )
      );
      _onStop();
    }
  }

  void _onDllFail(File dllPath) {
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if(_fail){
        return;
      }

      _fail = true;
      _closeDialogIfOpen(false);
      showMissingDllError(path.basename(dllPath.path));
      _onStop();
    });
  }

  Future<File> _getDllPath(Injectable injectable) async {
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

  Future<void> _downloadMissingDll(Injectable injectable) async {
    if(injectable != Injectable.reboot){
      await loadBinary("$injectable.dll", true);
      return;
    }

    await downloadRebootDll(0);
  }
}

enum Injectable {
  console,
  cranium,
  reboot,
  memoryFix
}
