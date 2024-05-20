import 'dart:collection';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/page/pages.dart';
import 'package:sync/semaphore.dart';

const infoBarLongDuration = Duration(seconds: 4);
const infoBarShortDuration = Duration(seconds: 2);

Semaphore _semaphore = Semaphore();
HashMap<int, _OverlayEntry> _overlays = HashMap();

void restoreMessage(int pageIndex, int lastIndex) {
  removeMessageByPage(lastIndex);
  final entry = _overlays[pageIndex];
  if(entry == null) {
    return;
  }

  Overlay.of(pageKey.currentContext!).insert(entry.overlay);
}

OverlayEntry showInfoBar(dynamic text,
    {InfoBarSeverity severity = InfoBarSeverity.info,
      bool loading = false,
      Duration? duration = infoBarShortDuration,
      void Function()? onDismissed,
      Widget? action}) {
  try {
    _semaphore.acquire();
    removeMessageByPage(pageIndex.value);
    final overlay = OverlayEntry(
        builder: (context) => Padding(
          padding: EdgeInsets.only(
              bottom: hasPageButton ? 72.0 : 16.0
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
    Overlay.of(pageKey.currentContext!).insert(overlay);
    _overlays[pageIndex.value] = _OverlayEntry(
        overlay: overlay,
        onDismissed: onDismissed
    );
    if(duration != null) {
      Future.delayed(duration).then((_) {
        WidgetsBinding.instance.addPostFrameCallback((timeStamp) async {
          final currentOverlay = _overlays[pageIndex.value];
          if(currentOverlay == overlay) {
            if(overlay.mounted) {
              overlay.remove();
            }

            _overlays.remove(pageIndex.value);
            currentOverlay?.onDismissed?.call();
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
  final lastOverlay = _overlays[index];
  if(lastOverlay != null) {
    try {
      lastOverlay.overlay.remove();
    }catch(_) {
      // Do not use .isMounted
      // This is intended behaviour
    }finally {
      _overlays.remove(index);
      lastOverlay.onDismissed?.call();
    }
  }
}

class _OverlayEntry {
  final OverlayEntry overlay;
  final void Function()? onDismissed;

  _OverlayEntry({required this.overlay, required this.onDismissed});
}