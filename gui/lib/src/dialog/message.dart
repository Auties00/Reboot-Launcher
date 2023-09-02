import 'package:fluent_ui/fluent_ui.dart' hide showDialog;

import 'package:reboot_launcher/src/page/home_page.dart';
import 'package:sync/semaphore.dart';

Semaphore _semaphore = Semaphore();
OverlayEntry? _lastOverlay;

void showMessage(String text, {InfoBarSeverity severity = InfoBarSeverity.info, bool loading = false, Duration? duration = snackbarShortDuration}) {
  try {
    _semaphore.acquire();
    if(_lastOverlay?.mounted == true) {
      _lastOverlay?.remove();
    }
    var pageIndexValue = pageIndex.value;
    _lastOverlay = showSnackbar(
        pageKey.currentContext!,
        InfoBar(
            title: Text(text),
            isLong: true,
            isIconVisible: true,
            content: SizedBox(
                width: double.infinity,
                child: loading ? const ProgressBar() : const SizedBox()
            ),
            severity: severity
        ),
        margin: EdgeInsets.only(
            left: 330.0,
            right: 16.0,
            bottom: pageIndexValue == 0 || pageIndexValue == 1 || pageIndexValue == 3 || pageIndexValue == 4 ? 72 : 16
        ),
        duration: duration
    );
  }finally {
    _semaphore.release();
  }
}