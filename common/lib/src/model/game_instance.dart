import 'dart:io';


class GameInstance {
  final String versionName;
  final int gamePid;
  final int? launcherPid;
  final int? eacPid;
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
  }

  bool get nestedHosting {
    GameInstance? child = this;
    while(child != null) {
      if(child.hosting) {
        return true;
      }

      child = child.child;
    }

    return false;
  }
}
