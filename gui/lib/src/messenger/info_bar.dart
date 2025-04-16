import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/page/pages.dart';

const infoBarLongDuration = Duration(seconds: 4);
const infoBarShortDuration = Duration(seconds: 2);
const _height = 64.0;

InfoBarEntry showRebootInfoBar(String text, {
  InfoBarSeverity severity = InfoBarSeverity.info,
  bool loading = false,
  Duration? duration = infoBarShortDuration,
  void Function()? onDismissed,
  Widget? action
}) {
  final overlay = _buildOverlay(text, action, loading, severity);
  final overlayEntry = InfoBarEntry(overlay: overlay, onDismissed: onDismissed);
  if(duration != null) {
    Future.delayed(duration)
        .then((_) => WidgetsBinding.instance.addPostFrameCallback((timeStamp) => overlayEntry.close()));
  }
  return overlayEntry;
}

Widget _buildOverlay(text, Widget? action, bool loading, InfoBarSeverity severity) => ConstrainedBox(
  constraints: BoxConstraints(
    minHeight: _height
  ),
  child: Mica(
      elevation: 1,
      child: InfoBar(
          title: Row(
            mainAxisSize: MainAxisSize.min,
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Expanded(
                  child: text is Widget ? text : Text(text)
              ),
              if(action != null)
                action
            ],
          ),
          isLong: false,
          isIconVisible: true,
          content: SizedBox(
              width: double.infinity,
              child: loading ? const Padding(
                padding: const EdgeInsets.only(
                    top: 8.0,
                    bottom: 2.0,
                    right: 6.0
                ),
                child: ProgressBar(),
              ) : const SizedBox()
          ),
          severity: severity
      )
  ),
);

class InfoBarEntry {
  final Widget overlay;
  final void Function()? onDismissed;

  InfoBarEntry({required this.overlay, required this.onDismissed}) {
    final context = pageKey.currentContext;
    if(context != null) {
      infoBarAreaKey.currentState?.insertChild(overlay);
    }
  }

  bool close() {
    final result = infoBarAreaKey.currentState?.removeChild(overlay) ?? false;
    if(result) {
      onDismissed?.call();
    }
    return result;
  }
}