import 'dart:async';
import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:get/get.dart';
import 'package:process_run/shell.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';
import 'package:reboot_launcher/src/model/game_type.dart';
import 'package:reboot_launcher/src/util/binary.dart';
import 'package:reboot_launcher/src/util/injector.dart';
import 'package:reboot_launcher/src/util/patcher.dart';
import 'package:reboot_launcher/src/util/server.dart';
import 'package:url_launcher/url_launcher.dart';
import 'package:win32_suspend_process/win32_suspend_process.dart';

import '../controller/settings_controller.dart';
import '../util/server_standalone.dart';

class LaunchButton extends StatefulWidget {
  const LaunchButton(
      {Key? key})
      : super(key: key);

  @override
  State<LaunchButton> createState() => _LaunchButtonState();
}

class _LaunchButtonState extends State<LaunchButton> {
  final GameController _gameController = Get.find<GameController>();
  final ServerController _serverController = Get.find<ServerController>();
  final SettingsController _settingsController = Get.find<SettingsController>();
  File? _logFile;
  bool _fail = false;

  @override
  void initState() {
    loadBinary("log.txt", true)
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
          message: _gameController.started.value ? "Close the running Fortnite instance" : "Launch a new Fortnite instance",
          child: Button(
              onPressed: _onPressed,
              child: Text(_gameController.started.value ? "Close" : "Launch")
          ),
        )),
      ),
    );
  }

  void _onPressed() async {
    if (_gameController.username.text.isEmpty) {
      showSnackbar(
          context, const Snackbar(content: Text("Please type a username")));
      _updateServerState(false);
      return;
    }

    if (_gameController.selectedVersionObs.value == null) {
      showSnackbar(
          context, const Snackbar(content: Text("Please select a version")));
      _updateServerState(false);
      return;
    }

    if (_gameController.started.value) {
      _onStop();
      return;
    }

    try {
      _updateServerState(true);
      var version = _gameController.selectedVersionObs.value!;
      var hosting = _gameController.type.value == GameType.headlessServer;
      if (version.launcher != null) {
        _gameController.launcherProcess = await Process.start(version.launcher!.path, []);
        Win32Process(_gameController.launcherProcess!.pid).suspend();
      }

      if (version.eacExecutable != null) {
        _gameController.eacProcess = await Process.start(version.eacExecutable!.path, []);
        Win32Process(_gameController.eacProcess!.pid).suspend();
      }

      if(hosting){
        await patchExe(version.executable!);
      }

      await _startServerIfNecessary();
      if(!_serverController.started.value){
        _onStop();
        return;
      }

      if(_logFile != null && await _logFile!.exists()){
        await _logFile!.delete();
      }

      var gamePath = version.executable?.path;
      if(gamePath == null){
        _onError("${version.location.path} no longer contains a Fortnite executable. Did you delete it?", null);
        _onStop();
        return;
      }

      _gameController.gameProcess = await Process.start(gamePath, createRebootArgs(_gameController.username.text, hosting))
        ..exitCode.then((_) => _onEnd())
        ..outLines.forEach(_onGameOutput);
      await _injectOrShowError(Injectable.cranium);

      if(hosting){
        await _showServerLaunchingWarning();
      }
    } catch (exception, stacktrace) {
      _closeDialogIfOpen(false);
      _onError(exception, stacktrace);
      _onStop();
    }
  }

  Future<void> _startServerIfNecessary() async {
    if (!mounted) {
      return;
    }

    if(_serverController.started.value){
      return;
    }

    if(!(await isLawinPortFree())){
      _serverController.started(true);
      return;
    }

    if (_serverController.embedded.value) {
      var result = await changeEmbeddedServerState(context, false);
      _serverController.started(result);
      return;
    }

    _serverController.reverseProxy = await changeReverseProxyState(
        context,
        _serverController.host.text,
        _serverController.port.text,
        _serverController.reverseProxy
    );
    _serverController.started(_serverController.reverseProxy != null);
  }

  Future<void> _updateServerState(bool value) async {
    if (_gameController.started.value == value) {
      return;
    }

    _gameController.started(value);
  }

  void _onEnd() {
    if(_fail){
      return;
    }

    _closeDialogIfOpen(false);
    _onStop();
  }

  void _closeDialogIfOpen(bool success) {
    if(!mounted){
      return;
    }

    var route = ModalRoute.of(context);
    if(route == null || route.isCurrent){
      return;
    }

    Navigator.of(context).pop(success);
  }

  Future<void> _showBrokenServerWarning() async {
    if(!mounted){
      return;
    }

    showDialog(
        context: context,
        builder: (context) => ContentDialog(
          content: const SizedBox(
              width: double.infinity,
              child: Text("The lawin server is not working correctly", textAlign: TextAlign.center)
          ),
          actions: [
            SizedBox(
                width: double.infinity,
                child: Button(
                  onPressed: () =>  Navigator.of(context).pop(),
                  child: const Text('Close'),
                )
            )
          ],
        )
    );
  }

  Future<void> _showTokenError() async {
    if(!mounted){
      return;
    }

    showDialog(
        context: context,
        builder: (context) => ContentDialog(
          content: const SizedBox(
              width: double.infinity,
              child: Text("A token error occurred, restart the game and the lawin server, then try again", textAlign: TextAlign.center)
          ),
          actions: [
            SizedBox(
                width: double.infinity,
                child: Button(
                  onPressed: () =>  Navigator.of(context).pop(),
                  child: const Text('Close'),
                )
            )
          ],
        )
    );
  }

  Future<void> _showUnsupportedHeadless() async {
    if(!mounted){
      return;
    }

    showDialog(
        context: context,
        builder: (context) => ContentDialog(
          content: const SizedBox(
              width: double.infinity,
              child: Text("This version of Fortnite doesn't support headless hosting", textAlign: TextAlign.center)
          ),
          actions: [
            SizedBox(
                width: double.infinity,
                child: Button(
                  onPressed: () =>  Navigator.of(context).pop(),
                  child: const Text('Close'),
                )
            )
          ],
        )
    );
  }

  Future<void> _showServerLaunchingWarning() async {
    if(!mounted){
      return;
    }

    var result = await showDialog<bool>(
        context: context,
        builder: (context) => ContentDialog(
          content: const InfoLabel(
              label: "Launching headless reboot server...",
              child: SizedBox(
                  width: double.infinity,
                  child: ProgressBar()
              )
          ),
          actions: [
            SizedBox(
                width: double.infinity,
                child: Button(
                  onPressed: () {
                    Navigator.of(context).pop(false);
                    _onStop();
                  },
                  child: const Text('Cancel'),
                )
            )
          ],
        )
    );

    if(result != null && result){
      return;
    }

    _onStop();
  }

  void _onGameOutput(String line) {
    if(kDebugMode){
      print(line);
    }

    if(_logFile != null){
      _logFile!.writeAsString("$line\n", mode: FileMode.append);
    }

    if (line.contains("FOnlineSubsystemGoogleCommon::Shutdown()")) {
      _onStop();
      return;
    }

    if(line.contains("port 3551 failed: Connection refused")){
      _fail = true;
      _closeDialogIfOpen(false);
      _showBrokenServerWarning();
      return;
    }

    if(line.contains("HTTP 400 response from ")){
      _fail = true;
      _closeDialogIfOpen(false);
      _showUnsupportedHeadless();
      return;
    }

    if(line.contains("Network failure when attempting to check platform restrictions")){
      _fail = true;
      _closeDialogIfOpen(false);
      _showTokenError();
      return;
    }

    if (line.contains("Game Engine Initialized") &&  _gameController.type.value == GameType.client) {
      _injectOrShowError(Injectable.console);
      return;
    }

    if(line.contains("Region") && _gameController.type.value != GameType.client){
      _injectOrShowError(Injectable.reboot)
          .then((value) => _closeDialogIfOpen(true));
    }
  }

  Future<Object?> _onError(Object exception, StackTrace? stackTrace) async {
    if (stackTrace != null) {
      var errorFile = await loadBinary("error.txt", true);
      errorFile.writeAsString(
          "Error: $exception\nStacktrace: $stackTrace", mode: FileMode.write);
      launchUrl(errorFile.uri);
    }

    return showDialog(
        context: context,
        builder: (context) => ContentDialog(
              content: SizedBox(
                  width: double.infinity,
                  child: Text("Cannot launch fortnite: $exception",
                      textAlign: TextAlign.center)),
              actions: [
                SizedBox(
                    width: double.infinity,
                    child: Button(
                      onPressed: () => Navigator.of(context).pop(true),
                      child: const Text('Close'),
                    ))
              ],
            ));
  }

  void _onStop() {
    _updateServerState(false);
    _gameController.kill();
  }

  Future<void> _injectOrShowError(Injectable injectable) async {
    var gameProcess = _gameController.gameProcess;
    if (gameProcess == null) {
      return;
    }

    try {
      var dllPath = _getDllPath(injectable);
      var success = await injectDll(gameProcess.pid, dllPath);
      if (success) {
        return;
      }

      _onInjectError(injectable.name);
    } catch (exception) {
      _onInjectError(injectable.name);
    }
  }

  String _getDllPath(Injectable injectable){
    switch(injectable){
      case Injectable.reboot:
        return _settingsController.rebootDll.text;
      case Injectable.console:
        return _settingsController.consoleDll.text;
      case Injectable.cranium:
        return _settingsController.craniumDll.text;
    }
  }

  void _onInjectError(String binary) {
    showSnackbar(context, Snackbar(content: Text("Cannot inject $binary")));
    launchUrl(injectLogFile.uri);
  }
}

enum Injectable {
  console,
  cranium,
  reboot
}
