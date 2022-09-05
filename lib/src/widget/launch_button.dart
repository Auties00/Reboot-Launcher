import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:process_run/shell.dart';
import 'package:reboot_launcher/src/util/game_process_controller.dart';
import 'package:reboot_launcher/src/util/generic_controller.dart';
import 'package:reboot_launcher/src/util/injector.dart';
import 'package:reboot_launcher/src/util/locate_binary.dart';
import 'package:url_launcher/url_launcher.dart';
import 'package:win32_suspend_process/win32_suspend_process.dart';

import '../util/server.dart';
import '../util/version_controller.dart';

class LaunchButton extends StatefulWidget {
  final TextEditingController usernameController;
  final VersionController versionController;
  final GenericController<bool> rebootController;
  final GenericController<bool> localController;
  final GenericController<Process?> serverController;
  final GameProcessController gameProcessController;
  final GenericController<bool> startedGameController;
  final GenericController<bool> startedServerController;

  const LaunchButton(
      {Key? key,
      required this.usernameController,
      required this.versionController,
      required this.rebootController,
      required this.serverController,
      required this.localController,
      required this.gameProcessController,
      required this.startedGameController,
        required this.startedServerController})
      : super(key: key);

  @override
  State<LaunchButton> createState() => _LaunchButtonState();
}

class _LaunchButtonState extends State<LaunchButton> {
  @override
  Widget build(BuildContext context) {
    return Align(
      alignment: AlignmentDirectional.bottomCenter,
      child: SizedBox(
        width: double.infinity,
        child: Listener(
          child: Button(
              onPressed: _onPressed,
              child: Text(widget.startedGameController.value
                  ? "Close"
                  : "Launch")),
        ),
      ),
    );
  }

  void _onPressed() async {
    // Set state immediately for responsive reasons
    if (widget.usernameController.text.isEmpty) {
      showSnackbar(
          context, const Snackbar(content: Text("Please type a username")));
      setState(() => widget.startedGameController.value = false);
      return;
    }

    if (widget.versionController.selectedVersion == null) {
      showSnackbar(
          context, const Snackbar(content: Text("Please select a version")));
      setState(() => widget.startedGameController.value = false);
      return;
    }

    if (widget.startedGameController.value) {
      _onStop();
      return;
    }

    if (widget.serverController.value == null && widget.localController.value && await isPortFree()) {
      var process = await startEmbedded(context, false, false);
      widget.serverController.value = process;
      widget.startedServerController.value = process != null;
    }

    _onStart();
    setState(() => widget.startedGameController.value = true);
  }

  Future<void> _onStart() async {
      try{
      var version = widget.versionController.selectedVersion!;

      if(await version.launcher.exists()) {
        widget.gameProcessController.launcherProcess =
        await Process.start(version.launcher.path, []);
        Win32Process(widget.gameProcessController.launcherProcess!.pid)
            .suspend();
      }

      if(await version.eacExecutable.exists()){
        widget.gameProcessController.eacProcess = await Process.start(version.eacExecutable.path, []);
        Win32Process(widget.gameProcessController.eacProcess!.pid).suspend();
      }

      widget.gameProcessController.gameProcess = await Process.start(widget.versionController.selectedVersion!.executable.path, _createProcessArguments())
        ..exitCode.then((_) => _onStop())
        ..outLines.forEach(_onGameOutput);
      _injectOrShowError("cranium.dll");
    }catch(exception){
      setState(() => widget.startedGameController.value = false);
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

      if (!widget.rebootController.value) {
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
    setState(() => widget.startedGameController.value = false);
    widget.gameProcessController.kill();
  }

  void _injectOrShowError(String binary) async {
    var gameProcess = widget.gameProcessController.gameProcess;
    if (gameProcess == null) {
      return;
    }

    try{
      var success = await injectDll(gameProcess.pid, await locateAndCopyBinary(binary));
      if(success){
        return;
      }

      _onInjectError(binary);
    }catch(exception){
      _onInjectError(binary);
    }
  }

  void _onInjectError(String binary) {
    showSnackbar(context, Snackbar(content: Text("Cannot inject $binary")));
    launchUrl(injectLogFile.uri);
  }

  List<String> _createProcessArguments() {
    return [
      "-log",
      "-epicapp=Fortnite",
      "-epicenv=Prod",
      "-epiclocale=en-us",
      "-epicportal",
      "-skippatchcheck",
      "-fromfl=eac",
      "-fltoken=3db3ba5dcbd2e16703f3978d",
      "-caldera=eyJhbGciOiJFUzI1NiIsInR5cCI6IkpXVCJ9.eyJhY2NvdW50X2lkIjoiYmU5ZGE1YzJmYmVhNDQwN2IyZjQwZWJhYWQ4NTlhZDQiLCJnZW5lcmF0ZWQiOjE2Mzg3MTcyNzgsImNhbGRlcmFHdWlkIjoiMzgxMGI4NjMtMmE2NS00NDU3LTliNTgtNGRhYjNiNDgyYTg2IiwiYWNQcm92aWRlciI6IkVhc3lBbnRpQ2hlYXQiLCJub3RlcyI6IiIsImZhbGxiYWNrIjpmYWxzZX0.VAWQB67RTxhiWOxx7DBjnzDnXyyEnX7OljJm-j2d88G_WgwQ9wrE6lwMEHZHjBd1ISJdUO1UVUqkfLdU5nofBQ",
      "-AUTH_LOGIN=${widget.usernameController.text}@projectreboot.dev",
      "-AUTH_PASSWORD=Rebooted",
      "-AUTH_TYPE=epic"
    ];
  }
}
