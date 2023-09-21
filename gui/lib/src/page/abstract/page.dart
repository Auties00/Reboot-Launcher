import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/dialog/abstract/info_bar.dart' as messenger;
import 'package:reboot_launcher/src/page/abstract/page_setting.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';

abstract class RebootPage extends StatefulWidget {
  const RebootPage({super.key});

  String get name;

  String get iconAsset;

  RebootPageType get type;

  int get index => type.index;

  List<PageSetting> get settings;

  bool get hasButton;

  @override
  RebootPageState createState();
}

abstract class RebootPageState<T extends RebootPage> extends State<T> with AutomaticKeepAliveClientMixin<T> {
  @override
  Widget build(BuildContext context) {
    super.build(context);
    var buttonWidget = button;
    if(buttonWidget == null) {
      return _listView;
    }

    return Column(
      children: [
        Expanded(
          child: _listView,
        ),
        const SizedBox(
          height: 8.0,
        ),
        ConstrainedBox(
            constraints: const BoxConstraints(
              maxWidth: 1000
            ),
            child: buttonWidget
        )
      ],
    );
  }

  OverlayEntry showInfoBar(dynamic text, {InfoBarSeverity severity = InfoBarSeverity.info, bool loading = false, Duration? duration = snackbarShortDuration, Widget? action}) => messenger.showInfoBar(
      text,
      pageType: widget.type,
      severity: severity,
      loading: loading,
      duration: duration,
      action: action
  );

  ListView get _listView => ListView.builder(
    itemCount: settings.length * 2,
    itemBuilder: (context, index) => index.isEven ? Align(
      alignment: Alignment.center,
      child: settings[index ~/ 2],
    ) : const SizedBox(height: 8.0),
  );

  @override
  bool get wantKeepAlive => true;

  List<Widget> get settings;

  Widget? get button;
}



