import 'dart:collection';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/page/pages.dart';
import 'package:sync/semaphore.dart';

Semaphore _semaphore = Semaphore();
HashMap<int, OverlayEntry?> _overlays = HashMap();

void restoreMessage(int pageIndex, int lastIndex) {
  removeMessageByPage(lastIndex);
  var overlay = _overlays[pageIndex];
  if(overlay == null) {
    return;
  }

  Overlay.of(pageKey.currentContext!).insert(overlay);
}

OverlayEntry showInfoBar(dynamic text,
    {RebootPageType? pageType,
      InfoBarSeverity severity = InfoBarSeverity.info,
      bool loading = false,
      Duration? duration = snackbarShortDuration,
      Widget? action}) {
  try {
    _semaphore.acquire();
    var index = pageType?.index ?? pageIndex.value;
    removeMessageByPage(index);
    var overlay = OverlayEntry(
        builder: (context) => Padding(
          padding: EdgeInsets.only(
              right: 12.0,
              left: 12.0,
              bottom: pagesWithButtonIndexes.contains(index) ? 72.0 : 16.0
          ),
          child: Align(
            alignment: AlignmentDirectional.bottomCenter,
            child: Container(
              width: double.infinity,
              constraints: const BoxConstraints(
                  maxWidth: 1000
              ),
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
          ),
        )
    );
    if(index == pageIndex.value) {
      Overlay.of(pageKey.currentContext!).insert(overlay);
    }
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
    return overlay;
  }finally {
    _semaphore.release();
  }
}

void removeMessageByPage(int index) {
  var lastOverlay = _overlays[index];
  if(lastOverlay != null) {
    removeMessageByOverlay(lastOverlay);
    _overlays[index] = null;
  }
}

void removeMessageByOverlay(OverlayEntry? overlay) {
  try {
    if(overlay != null) {
      overlay.remove();
    }
  }catch(_) {
    // Do not use .isMounted
    // This is intended behaviour
  }
}