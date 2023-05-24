import 'package:get_storage/get_storage.dart';

final GetStorage _storage = GetStorage("reboot_update");

int? get updateTime => _storage.read("last_update_v2");
set updateTime(int? updateTime) =>  _storage.write("last_update_v2", updateTime);