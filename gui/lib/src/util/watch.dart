import 'dart:io';

import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/hosting_controller.dart';
import 'package:supabase_flutter/supabase_flutter.dart';

final SupabaseClient _supabase = Supabase.instance.client;
final GameController _gameController = Get.find<GameController>();
final HostingController _hostingController = Get.find<HostingController>();

extension GameInstanceWatcher on GameInstance {
  Future<void> startObserver() async {
    if(watchPid != null) {
      Process.killPid(watchPid!, ProcessSignal.sigabrt);
    }

    watchProcess(gamePid).then((value) async {
      if(hosting) {
        _onHostingStopped();
      }

      _onGameStopped();
    });

    watchPid = startBackgroundProcess(
        '${assetsDirectory.path}\\misc\\watch.exe',
        [_gameController.uuid, gamePid.toString(), launcherPid?.toString() ?? "-1", eacPid?.toString() ?? "-1", hosting.toString()]
    );
  }

  void _onGameStopped() {
    _gameController.started.value = false;
    _gameController.instance.value?.kill();
    if(linkedHosting) {
      _onHostingStopped();
    }
  }

  Future<void> _onHostingStopped() async {
    _hostingController.started.value = false;
    _hostingController.instance.value?.kill();
    await _supabase.from('hosts')
        .delete()
        .match({'id': _gameController.uuid});
  }
}