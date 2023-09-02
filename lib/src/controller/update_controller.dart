import 'package:fluent_ui/fluent_ui.dart' hide showDialog;
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_common/common.dart';

class UpdateController {
  late final GetStorage _storage;
  late final RxnInt timestamp;
  late final Rx<UpdateStatus> status;
  late final Rx<UpdateTimer> timer;
  late final TextEditingController url;

  UpdateController() {
    _storage = GetStorage("reboot_update");
    timestamp = RxnInt(_storage.read("ts"));
    timestamp.listen((value) => _storage.write("ts", value));
    var timerIndex = _storage.read("timer");
    timer = Rx(timerIndex == null ? UpdateTimer.never : UpdateTimer.values.elementAt(timerIndex));
    timer.listen((value) => _storage.write("timer", value.index));
    url = TextEditingController(text: _storage.read("update_url") ?? rebootDownloadUrl);
    url.addListener(() => _storage.write("update_url", url.text));
    status = Rx(UpdateStatus.waiting);
  }

  Future<void> update() async {
    if(timer.value == UpdateTimer.never) {
      status.value = UpdateStatus.success;
      return;
    }

    try {
      timestamp.value = await downloadRebootDll(url.text, timestamp.value);
      status.value = UpdateStatus.success;
    }catch(_) {
      status.value = UpdateStatus.error;
      rethrow;
    }
  }

  void reset() {
    timestamp.value = null;
    timer.value = UpdateTimer.never;
    url.text = rebootDownloadUrl;
    status.value = UpdateStatus.waiting;
    update();
  }
}