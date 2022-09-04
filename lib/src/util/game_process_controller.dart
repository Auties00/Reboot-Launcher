import 'dart:io';

class GameProcessController {
  Process? gameProcess;
  Process? launcherProcess;
  Process? eacProcess;

  void kill(){
    gameProcess?.kill(ProcessSignal.sigabrt);
    launcherProcess?.kill(ProcessSignal.sigabrt);
    eacProcess?.kill(ProcessSignal.sigabrt);
  }
}
