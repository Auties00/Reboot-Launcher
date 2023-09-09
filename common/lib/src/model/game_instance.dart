import 'dart:io';


class GameInstance {
  final int gamePid;
  final int? launcherPid;
  final int? eacPid;
  int? observerPid;
  bool hosting;
  bool tokenError;
  bool linkedHosting;

  GameInstance(this.gamePid, this.launcherPid, this.eacPid, this.hosting, this.linkedHosting)
      : tokenError = false,
        assert(!linkedHosting || !hosting, "Only a game instance can have a linked hosting server");

  GameInstance._fromJson(this.gamePid, this.launcherPid, this.eacPid, this.observerPid,
      this.hosting, this.tokenError, this.linkedHosting);

  static GameInstance? fromJson(Map<String, dynamic>? json) {
    if(json == null) {
      return null;
    }

    var gamePid = json["game"];
    if(gamePid == null) {
      return null;
    }

    var launcherPid = json["launcher"];
    var eacPid = json["eac"];
    var observerPid = json["observer"];
    var hosting = json["hosting"];
    var tokenError = json["tokenError"];
    var linkedHosting = json["linkedHosting"];
    return GameInstance._fromJson(gamePid, launcherPid, eacPid, observerPid, hosting, tokenError, linkedHosting);
  }

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
  }

  Map<String, dynamic> toJson() => {
    'game': gamePid,
    'launcher': launcherPid,
    'eac': eacPid,
    'observer': observerPid,
    'hosting': hosting,
    'tokenError': tokenError,
    'linkedHosting': linkedHosting
  };
}
