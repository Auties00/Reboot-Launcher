import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/dialog/abstract/info_bar.dart';
import 'package:reboot_launcher/src/util/translations.dart';

class UpdateController {
  late final GetStorage _storage;
  late final RxnInt timestamp;
  late final Rx<UpdateStatus> status;
  late final Rx<UpdateTimer> timer;
  late final TextEditingController url;
  InfoBarEntry? infoBarEntry;
  Future? _updater;

  UpdateController() {
    _storage = GetStorage("update");
    timestamp = RxnInt(_storage.read("ts"));
    timestamp.listen((value) => _storage.write("ts", value));
    var timerIndex = _storage.read("timer");
    timer = Rx(timerIndex == null ? UpdateTimer.day : UpdateTimer.values.elementAt(timerIndex));
    timer.listen((value) => _storage.write("timer", value.index));
    url = TextEditingController(text: _storage.read("update_url") ?? kRebootDownloadUrl);
    url.addListener(() => _storage.write("update_url", url.text));
    status = Rx(UpdateStatus.waiting);
  }

  Future<void> update([bool force = false]) async {
    if(_updater != null) {
      return await _updater;
    }

    final result = _update(force);
    _updater = result;
    return await result;
  }

  Future<void> _update([bool force = false]) async {
    try {
      final needsUpdate = await hasRebootDllUpdate(
          timestamp.value,
          hours: timer.value.hours,
          force: force
      );
      if(!needsUpdate) {
        status.value = UpdateStatus.success;
        return;
      }

      infoBarEntry = showInfoBar(
          translations.downloadingDll("reboot"),
          loading: true,
          duration: null
      );
      timestamp.value = await downloadRebootDll(url.text);
      status.value = UpdateStatus.success;
      infoBarEntry?.close();
      infoBarEntry = showInfoBar(
          translations.downloadDllSuccess("reboot"),
          severity: InfoBarSeverity.success,
          duration: infoBarShortDuration
      );
    }catch(message) {
      infoBarEntry?.close();
      var error = message.toString();
      error = error.contains(": ") ? error.substring(error.indexOf(": ") + 2) : error;
      error = error.toLowerCase();
      status.value = UpdateStatus.error;
      showInfoBar(
          translations.downloadDllError("reboot.dll", error.toString()),
          duration: infoBarLongDuration,
          severity: InfoBarSeverity.error,
          action: Button(
            onPressed: () => update(true),
            child: Text(translations.downloadDllRetry),
          )
      );
    }finally {
      _updater = null;
    }
  }

  void reset() {
    timestamp.value = null;
    timer.value = UpdateTimer.never;
    url.text = kRebootDownloadUrl;
    status.value = UpdateStatus.waiting;
    update();
  }
}

extension _UpdateTimerExtension on UpdateTimer {
  int get hours {
    switch(this) {
      case UpdateTimer.never:
        return -1;
      case UpdateTimer.hour:
        return 1;
      case UpdateTimer.day:
        return 24;
      case UpdateTimer.week:
        return 24 * 7;
    }
  }
}