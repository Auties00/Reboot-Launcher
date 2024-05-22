import 'dart:io';

import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/hosting_controller.dart';
import 'package:supabase_flutter/supabase_flutter.dart';

final File _executable = File("${assetsDirectory.path}\\misc\\watch.exe");

extension GameInstanceWatcher on GameInstance {
  Future<void> startObserver() async {
    if(observerPid != null) {
      Process.killPid(observerPid!, ProcessSignal.sigabrt);
    }

    final hostingController = Get.find<HostingController>();
    final gameController = Get.find<GameController>();
    watchProcess(gamePid).then((value) async {
      gameController.started.value = false;
      gameController.instance.value?.kill();
      if(_nestedHosting) {
        hostingController.started.value = false;
        hostingController.instance.value?.kill();
        await Supabase.instance.client.from("hosting")
            .delete()
            .match({'id': hostingController.uuid});
      }
    });

    final process = await startProcess(
        executable: _executable,
        args: [
          hostingController.uuid,
          gamePid.toString(),
          launcherPid?.toString() ?? "-1",
          eacPid?.toString() ?? "-1",
          hosting.toString()
        ],
        
    );
    observerPid = process.pid;
  }

  bool get _nestedHosting {
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