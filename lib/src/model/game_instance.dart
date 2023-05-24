import 'dart:io';

class GameInstance {
  final Process gameProcess;
  final Process? launcherProcess;
  final Process? eacProcess;
  bool tokenError;
  bool hasChildServer;

  GameInstance(this.gameProcess, this.launcherProcess, this.eacProcess, this.hasChildServer)
      : tokenError = false;

  void kill() {
    gameProcess.kill(ProcessSignal.sigabrt);
    launcherProcess?.kill(ProcessSignal.sigabrt);
    eacProcess?.kill(ProcessSignal.sigabrt);
  }
}
