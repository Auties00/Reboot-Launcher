import 'dart:io';

class GameInstance {
  final Process gameProcess;
  final Process? launcherProcess;
  final Process? eacProcess;

  GameInstance(this.gameProcess, this.launcherProcess, this.eacProcess);

  void kill() {
    gameProcess.kill(ProcessSignal.sigabrt);
    launcherProcess?.kill(ProcessSignal.sigabrt);
    eacProcess?.kill(ProcessSignal.sigabrt);
  }
}
