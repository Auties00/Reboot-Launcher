import 'dart:io';


class GameInstance {
  final String versionName;
  final int gamePid;
  final int? launcherPid;
  final int? eacPid;
  int? observerPid;
  bool hosting;
  bool launched;
  bool tokenError;
  GameInstance? child;

  GameInstance({
    required this.versionName,
    required this.gamePid,
    required this.launcherPid,
    required this.eacPid,
    required this.hosting,
    required this.child
  }): tokenError = false, launched = false;

  void kill() {
    Process.killPid(gamePid, ProcessSignal.sigabrt);
    if(launcherPid != null) {
      Process.killPid(launcherPid!, ProcessSignal.sigabrt);
    }
    if(eacPid != null) {
      Process.killPid(eacPid!, ProcessSignal.sigabrt);
    }
    if(observerPid != null) {
      Process.killPid(observerPid!, ProcessSignal.sigabrt);
    }
    child?.kill();
  }
}
