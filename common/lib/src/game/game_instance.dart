import 'dart:io';

import 'package:reboot_common/common.dart';


class GameInstance {
  final String version;
  final bool host;
  final int gamePid;
  final int? launcherPid;
  final int? eacPid;
  final List<GameDll> injectedDlls;
  final bool headless;
  bool launched;
  bool tokenError;
  bool killed;
  GameInstance? child;

  GameInstance({
    required this.version,
    required this.host,
    required this.gamePid,
    required this.launcherPid,
    required this.eacPid,
    required this.headless,
    required this.child
  }): tokenError = false, killed = false, launched = false, injectedDlls = [];

  void kill() {
    GameInstance? child = this;
    while(child != null) {
      child._kill();
      child = child.child;
    }
  }

  void _kill() {
    if(!killed) {
      launched = true;
      killed = true;
      Process.killPid(gamePid, ProcessSignal.sigabrt);
      if (launcherPid != null) {
        Process.killPid(launcherPid!, ProcessSignal.sigabrt);
      }
      if (eacPid != null) {
        Process.killPid(eacPid!, ProcessSignal.sigabrt);
      }
    }
  }
}