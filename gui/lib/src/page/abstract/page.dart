import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';

abstract class RebootPage extends StatefulWidget {
  const RebootPage({super.key});

  String get name;

  String get iconAsset;

  RebootPageType get type;

  int get index => type.index;

  bool hasButton(String? pageName);

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

  ListView get _listView => ListView.builder(
    itemCount: settings.length,
    itemBuilder: (context, index) => settings[index],
  );

  @override
  bool get wantKeepAlive => true;

  List<Widget> get settings;

  Widget? get button;
}



