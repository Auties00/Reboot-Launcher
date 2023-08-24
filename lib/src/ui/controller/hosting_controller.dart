import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_launcher/src/ui/controller/settings_controller.dart';
import 'package:reboot_launcher/src/ui/controller/update_controller.dart';

import 'package:reboot_launcher/src/model/game_instance.dart';
import 'package:reboot_launcher/src/model/update_status.dart';
import 'package:reboot_launcher/src/util/reboot.dart';


const String kDefaultServerName = "Reboot Game Server";

class HostingController extends GetxController {
  late final GetStorage _storage;
  late final TextEditingController name;
  late final TextEditingController description;
  late final RxBool discoverable;
  late final RxBool started;
  late final Rx<UpdateStatus> updateStatus;
  GameInstance? instance;

  HostingController() {
    _storage = GetStorage("reboot_hosting");
    name = TextEditingController(text: _storage.read("name") ?? kDefaultServerName);
    name.addListener(() => _storage.write("name", name.text));
    description = TextEditingController(text: _storage.read("description") ?? "");
    description.addListener(() => _storage.write("description", description.text));
    discoverable = RxBool(_storage.read("discoverable") ?? false);
    discoverable.listen((value) => _storage.write("discoverable", value));
    updateStatus = Rx(UpdateStatus.waiting);
    started = RxBool(false);
    startUpdater();
  }

  Future<void> startUpdater() async {
    var settings = Get.find<SettingsController>();
    if(!settings.autoUpdate()){
      updateStatus.value = UpdateStatus.success;
      return;
    }

    updateStatus.value = UpdateStatus.started;
    try {
      updateTime = await downloadRebootDll(settings.updateUrl.text, updateTime);
      updateStatus.value = UpdateStatus.success;
    }catch(_) {
      updateStatus.value = UpdateStatus.error;
      rethrow;
    }
  }
}
