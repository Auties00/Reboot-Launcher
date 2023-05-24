import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';

import '../../model/game_instance.dart';


const String kDefaultServerName = "Reboot Game Server";

class HostingController extends GetxController {
  late final GetStorage _storage;
  late final TextEditingController name;
  late final TextEditingController category;
  late final RxBool discoverable;
  late final RxBool started;
  GameInstance? instance;

  HostingController() {
    _storage = GetStorage("reboot_hosting");
    name = TextEditingController(text: _storage.read("name") ?? kDefaultServerName);
    name.addListener(() => _storage.write("name", name.text));
    category = TextEditingController(text: _storage.read("category") ?? "");
    category.addListener(() => _storage.write("category", category.text));
    discoverable = RxBool(_storage.read("discoverable") ?? false);
    discoverable.listen((value) => _storage.write("discoverable", value));
    started = RxBool(false);
  }
}
