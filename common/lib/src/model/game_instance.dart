import 'dart:io';

import 'package:reboot_common/common.dart';


class GameInstance {
  final String versionName;
  final int gamePid;
  final int? launcherPid;
  final int? eacPid;
  final List<InjectableDll> injectedDlls;
  final GameServerType? serverType;
  bool launched;
  bool movedToVirtualDesktop;
  bool tokenError;
  bool killed;
  GameInstance? child;

  GameInstance({
    required this.versionName,
    required this.gamePid,
    required this.launcherPid,
    required this.eacPid,
    required this.serverType,
    required this.child
  }): tokenError = false, killed = false, launched = false, movedToVirtualDesktop = false, injectedDlls = [];

  void kill() {
    GameInstance? child = this;
    while(child != null) {
      child._kill();
      child = child.child;
    }
  }

  void _kill() {
    launched = true;
    killed = true;
    Process.killPid(gamePid, ProcessSignal.sigabrt);
    if(launcherPid != null) {
      Process.killPid(launcherPid!, ProcessSignal.sigabrt);
    }
    if(eacPid != null) {
      Process.killPid(eacPid!, ProcessSignal.sigabrt);
    }
  }
}

enum GameServerType {
  headless,
  virtualWindow,
  window
}