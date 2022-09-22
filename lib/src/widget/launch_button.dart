import 'dart:async';
import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:process_run/shell.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';
import 'package:reboot_launcher/src/util/injector.dart';
import 'package:reboot_launcher/src/util/binary.dart';
import 'package:url_launcher/url_launcher.dart';
import 'package:win32_suspend_process/win32_suspend_process.dart';

import 'package:reboot_launcher/src/util/server.dart';

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

  @override
  Widget build(BuildContext context) {
    return Align(
      alignment: AlignmentDirectional.bottomCenter,
      child: SizedBox(
        width: double.infinity,
        child: Obx(() => Tooltip(
          message: _gameController.started.value ? "Close the running Fortnite instance" : "Launch a new Fortnite instance",
          child: Button(
              onPressed: () => _onPressed(context),
              child: Text(_gameController.started.value ? "Close" : "Launch")
          ),
        )),
      ),
    );
  }

  void _onPressed(BuildContext context) async {
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

    _updateServerState(true);
    if (!_serverController.started.value && _serverController.embedded.value && await isLawinPortFree()) {
      var result = await changeEmbeddedServerState(context, false);
      _serverController.started(result);
    }

    _onStart();
  }

  Future<void> _updateServerState(bool value) async {
    if (_gameController.started.value == value) {
      return;
    }

    _gameController.started(value);
  }

  Future<void> _onStart() async {
    try {
      _updateServerState(true);
      var version = _gameController.selectedVersionObs.value!;
      if (await version.launcher.exists()) {
        _gameController.launcherProcess = await Process.start(version.launcher.path, []);
        Win32Process(_gameController.launcherProcess!.pid).suspend();
      }

      if (await version.eacExecutable.exists()) {
        _gameController.eacProcess = await Process.start(version.eacExecutable.path, []);
        Win32Process(_gameController.eacProcess!.pid).suspend();
      }

      _gameController.gameProcess = await Process.start(version.executable.path, _createProcessArguments())
        ..exitCode.then((_) => _onStop())
        ..outLines.forEach(_onGameOutput);
      _injectOrShowError("cranium.dll");
    } catch (exception) {
      _updateServerState(false);
      _onError(exception);
    }
  }

  void _onGameOutput(line) {
    if (line.contains("FOnlineSubsystemGoogleCommon::Shutdown()")) {
      _onStop();
      return;
    }

    if (!line.contains("Game Engine Initialized")) {
      return;
    }

    if (!_gameController.host.value) {
      _injectOrShowError("console.dll");
      return;
    }

    _injectOrShowError("reboot.dll");
  }

  Future<Object?> _onError(exception) {
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
                    child: FilledButton(
                      onPressed: () => Navigator.of(context).pop(),
                      style: ButtonStyle(
                          backgroundColor: ButtonState.all(Colors.red)),
                      child: const Text('Close'),
                    ))
              ],
            ));
  }

  void _onStop() {
    _updateServerState(false);
    _gameController.kill();
  }

  void _injectOrShowError(String binary) async {
    var gameProcess = _gameController.gameProcess;
    if (gameProcess == null) {
      return;
    }

    try {
      var dll = await loadBinary(binary, true);
      var success = await injectDll(gameProcess.pid, dll.path);
      if (success) {
        return;
      }

      _onInjectError(binary);
    } catch (exception) {
      _onInjectError(binary);
    }
  }

  void _onInjectError(String binary) {
    showSnackbar(context, Snackbar(content: Text("Cannot inject $binary")));
    launchUrl(injectLogFile.uri);
  }

  List<String> _createProcessArguments() {
    return [
      "-epicapp=Fortnite",
      "-epicenv=Prod",
      "-epiclocale=en-us",
      "-epicportal",
      "-skippatchcheck",
      "-fromfl=eac",
      "-fltoken=3db3ba5dcbd2e16703f3978d",
      "-caldera=eyJhbGciOiJFUzI1NiIsInR5cCI6IkpXVCJ9.eyJhY2NvdW50X2lkIjoiYmU5ZGE1YzJmYmVhNDQwN2IyZjQwZWJhYWQ4NTlhZDQiLCJnZW5lcmF0ZWQiOjE2Mzg3MTcyNzgsImNhbGRlcmFHdWlkIjoiMzgxMGI4NjMtMmE2NS00NDU3LTliNTgtNGRhYjNiNDgyYTg2IiwiYWNQcm92aWRlciI6IkVhc3lBbnRpQ2hlYXQiLCJub3RlcyI6IiIsImZhbGxiYWNrIjpmYWxzZX0.VAWQB67RTxhiWOxx7DBjnzDnXyyEnX7OljJm-j2d88G_WgwQ9wrE6lwMEHZHjBd1ISJdUO1UVUqkfLdU5nofBQ",
      "-AUTH_LOGIN=${_gameController.username.text}@projectreboot.dev",
      "-AUTH_PASSWORD=Rebooted",
      "-AUTH_TYPE=epic"
    ];
  }
}
