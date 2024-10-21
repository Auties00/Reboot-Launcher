import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/messenger/implementation/onboard.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/util/translations.dart';

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
  final SettingsController _settingsController = Get.find<SettingsController>();

  @override
  Widget build(BuildContext context) {
    super.build(context);
    var buttonWidget = button;
    if(buttonWidget == null) {
      return Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          _buildFirstLaunchInfo(),
          Expanded(
            child: _listView
          )
        ],
      );
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        _buildFirstLaunchInfo(),
        Expanded(
          child: Column(
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
          ),
        ),
      ],
    );
  }

  Widget _buildFirstLaunchInfo() => Obx(() {
    if(!_settingsController.firstRun.value) {
      return const SizedBox.shrink();
    }

    return Padding(
      padding: const EdgeInsets.only(
          bottom: 8.0
      ),
      child: SizedBox(
        width: double.infinity,
        child: InfoBar(
            title: Text(translations.welcomeTitle),
            severity: InfoBarSeverity.warning,
            isLong: true,
            content: SizedBox(
                width: double.infinity,
                child: Text(translations.welcomeDescription)
            ),
            action: Button(
              child: Text(translations.welcomeAction),
              onPressed: () => startOnboarding(),
            ),
            onClose: () => _settingsController.firstRun.value = false
        ),
      ),
    );
  });

  ListView get _listView => ListView.builder(
    itemCount: settings.length,
    itemBuilder: (context, index) => settings[index],
  );

  @override
  bool get wantKeepAlive => true;

  List<Widget> get settings;

  Widget? get button;
}



