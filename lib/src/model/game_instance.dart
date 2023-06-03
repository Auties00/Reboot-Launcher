import 'dart:io';

class GameInstance {
  final Process gameProcess;
  final Process? launcherProcess;
  final Process? eacProcess;
  final int? watchDogProcessPid;
  bool tokenError;
  bool hasChildServer;

  GameInstance(this.gameProcess, this.launcherProcess, this.eacProcess, this.watchDogProcessPid, this.hasChildServer)
      : tokenError = false;

  void kill() {
    gameProcess.kill(ProcessSignal.sigabrt);
    launcherProcess?.kill(ProcessSignal.sigabrt);
    eacProcess?.kill(ProcessSignal.sigabrt);
    if(watchDogProcessPid != null){
      Process.killPid(watchDogProcessPid!, ProcessSignal.sigabrt);
    }
  }
}
