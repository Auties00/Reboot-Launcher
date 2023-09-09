import 'dart:collection';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/page/home_page.dart';
import 'package:sync/semaphore.dart';

Semaphore _semaphore = Semaphore();
HashMap<int, OverlayEntry?> _overlays = HashMap();

void restoreMessage(int lastIndex) {
  removeMessage(lastIndex);
  var overlay = _overlays[pageIndex.value];
  if(overlay == null) {
    return;
  }

  Overlay.of(pageKey.currentContext!).insert(overlay);
}

void showInfoBar(dynamic text, {InfoBarSeverity severity = InfoBarSeverity.info, bool loading = false, Duration? duration = snackbarShortDuration, Widget? action}) {
  try {
    _semaphore.acquire();
    var index = pageIndex.value;
    removeMessage(index);
    var overlay = showSnackbar(
        pageKey.currentContext!,
        SizedBox(
          width: double.infinity,
          child: Mica(
            child: InfoBar(
                title: Row(
                  mainAxisAlignment: MainAxisAlignment.spaceBetween,
                  children: [
                    if(text is Widget)
                      text,
                    if(text is String)
                      Text(text),
                    if(action != null)
                      action
                  ],
                ),
                isLong: false,
                isIconVisible: true,
                content: SizedBox(
                    width: double.infinity,
                    child: loading ? const Padding(
                      padding: EdgeInsets.only(top: 8.0, bottom: 2.0),
                      child: ProgressBar(),
                    ) : const SizedBox()
                ),
                severity: severity
            ),
          ),
        ),
        margin: EdgeInsets.only(
            right: 12.0,
            left: 12.0,
            bottom: index == 0 || index == 1 || index == 3 || index == 4 ? 72.0 : 16.0
        ),
        duration: duration
    );
    _overlays[index] = overlay;
    if(duration != null) {
      Future.delayed(duration).then((_) {
        WidgetsBinding.instance.addPostFrameCallback((timeStamp) async {
          if(_overlays[index] == overlay) {
            if(overlay.mounted) {
              overlay.remove();
            }

            _overlays[index] = null;
          }
        });
      });
    }
  }finally {
    _semaphore.release();
  }
}

void removeMessage(int index) {
  try {
    var lastOverlay = _overlays[index];
    if(lastOverlay != null) {
      lastOverlay.remove();
      _overlays[index] = null;
    }
  }catch(_) {
    // Do not use .isMounted
    // This is intended behaviour
  }
}