import 'dart:io';

import 'package:process_run/process_run.dart';
import 'package:reboot_cli/cli.dart';
import 'package:reboot_common/common.dart';

Process? _gameProcess;
Process? _launcherProcess;
Process? _eacProcess;

Future<void> startGame() async {
  await _startLauncherProcess(version);
  await _startEacProcess(version);

  var executable = await version.gameExecutable;
  if (executable == null) {
    throw Exception("${version.location.path} no longer contains a Fortnite executable, did you delete or move it?");
  }

  if (username == null) {
    username = "Reboot${host ? 'Host' : 'Player'}";
    stdout.writeln("No username was specified, using $username by default. Use --username to specify one");
  }

  _gameProcess = await Process.start(executable.path, createRebootArgs(username!, "", host, host, ""))
    ..exitCode.then((_) => _onClose())
    ..stdOutput.forEach((line) => _onGameOutput(line, dll, host, verbose));
  _injectOrShowError("cobalt.dll");
}


Future<void> _startLauncherProcess(FortniteVersion dummyVersion) async {
  if (dummyVersion.launcherExecutable == null) {
    return;
  }

  _launcherProcess = await Process.start(dummyVersion.launcherExecutable!.path, []);
  suspend(_launcherProcess!.pid);
}

Future<void> _startEacProcess(FortniteVersion dummyVersion) async {
  if (dummyVersion.eacExecutable == null) {
    return;
  }

  _eacProcess = await Process.start(dummyVersion.eacExecutable!.path, []);
  suspend(_eacProcess!.pid);
}

void _onGameOutput(String line, String dll, bool hosting, bool verbose) {
  if(verbose) {
    stdout.writeln(line);
  }

  handleGameOutput(
      line: line,
      host: hosting,
      onDisplayAttached: () {}, // TODO: Support virtual desktops
      onLoggedIn: onLoggedIn,
      onMatchEnd: onMatchEnd,
      onShutdown: onShutdown,
      onTokenError: onTokenError,
      onBuildCorrupted: onBuildCorrupted
  );

  if (line.contains(kShutdownLine)) {
    _onClose();
    return;
  }

  if(kCannotConnectErrors.any((element) => line.contains(element))){
    stderr.writeln("The backend doesn't work! Token expired");
    _onClose();
    return;
  }

  if(line.contains("Region ")){
    if(hosting) {
      _injectOrShowError(dll, false);
    }else {
      _injectOrShowError("console.dll");
    }

    _injectOrShowError("memory.dll");
  }
}

void _kill() {
  _gameProcess?.kill(ProcessSignal.sigabrt);
  _launcherProcess?.kill(ProcessSignal.sigabrt);
  _eacProcess?.kill(ProcessSignal.sigabrt);
}

Future<void> _injectOrShowError(String binary, [bool locate = true]) async {
  if (_gameProcess == null) {
    return;
  }

  try {
    stdout.writeln("Injecting $binary...");
    var dll = locate ? File("${dllsDirectory.path}\\$binary") : File(binary);
    if(!dll.existsSync()){
      throw Exception("Cannot inject $dll: missing file");
    }

    await injectDll(_gameProcess!.pid, dll);
  } catch (exception) {
    throw Exception("Cannot inject binary: $binary");
  }
}

void _onClose() {
  _kill();
  sleep(const Duration(seconds: 3));
  stdout.writeln("The game was closed");
  if(autoRestart){
    stdout.writeln("Restarting automatically game");
    startGame();
    return;
  }

  exit(0);
}