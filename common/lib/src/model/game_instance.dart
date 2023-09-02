import 'dart:io';


class GameInstance {
  final int gamePid;
  final int? launcherPid;
  final int? eacPid;
  int? watchPid;
  bool hosting;
  bool tokenError;
  bool linkedHosting;

  GameInstance(this.gamePid, this.launcherPid, this.eacPid, this.hosting, this.linkedHosting)
      : tokenError = false,
        assert(!linkedHosting || !hosting, "Only a game instance can have a linked hosting server");

  GameInstance.fromJson(Map<String, dynamic>? json) :
        gamePid = json?["game"] ?? -1,
        launcherPid = json?["launcher"],
        eacPid = json?["eac"],
        watchPid = json?["watchPid"],
        hosting = json?["hosting"] ?? false,
        tokenError = json?["tokenError"] ?? false,
        linkedHosting = json?["linkedHosting"] ?? false;

  void kill() {
    Process.killPid(gamePid, ProcessSignal.sigabrt);
    if(launcherPid != null) {
      Process.killPid(launcherPid!, ProcessSignal.sigabrt);
    }
    if(eacPid != null) {
      Process.killPid(eacPid!, ProcessSignal.sigabrt);
    }
    if(watchPid != null) {
      Process.killPid(watchPid!, ProcessSignal.sigabrt);
    }
  }

  Map<String, dynamic> toJson() => {
    'game': gamePid,
    'launcher': launcherPid,
    'eac': eacPid,
    'watch': watchPid,
    'hosting': hosting,
    'tokenError': tokenError,
    'linkedHosting': linkedHosting
  };
}
