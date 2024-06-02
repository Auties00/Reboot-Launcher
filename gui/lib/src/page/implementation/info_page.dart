import 'dart:async';

import 'package:fluent_ui/fluent_ui.dart' hide FluentIcons;
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/page/abstract/page.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/page/pages.dart';
import 'package:reboot_launcher/src/util/info.dart';
import 'package:reboot_launcher/src/util/translations.dart';

class InfoPage extends RebootPage {
  const InfoPage({Key? key}) : super(key: key);

  @override
  RebootPageState<InfoPage> createState() => _InfoPageState();

  @override
  String get name => translations.infoName;

  @override
  String get iconAsset => "assets/images/info.png";

  @override
  bool hasButton(String? routeName) => false;

  @override
  RebootPageType get type => RebootPageType.info;
}

class _InfoPageState extends RebootPageState<InfoPage> {
  final SettingsController _settingsController = Get.find<SettingsController>();
  RxInt _counter = RxInt(180);

  @override
  void initState() {
    if(_settingsController.firstRun.value) {
      Timer.periodic(const Duration(seconds: 1), (timer) {
        if (_counter.value <= 0) {
          _settingsController.firstRun.value = false;
          timer.cancel();
        } else {
          _counter.value = _counter.value - 1;
        }
      });
    }
    super.initState();
  }

  @override
  List<Widget> get settings => infoTiles;

  @override
  Widget? get button => Obx(() {
    if(!_settingsController.firstRun.value) {
      return const SizedBox.shrink();
    }

    final totalSecondsLeft = _counter.value;
    final minutesLeft = totalSecondsLeft ~/ 60;
    final secondsLeft = totalSecondsLeft % 60;
    return SizedBox(
        width: double.infinity,
        height: 48,
        child: Button(
          onPressed: totalSecondsLeft <= 0 ? () => pageIndex.value = RebootPageType.play.index : null,
          child: Text(
              totalSecondsLeft <= 0 ? "I have read the instructions"
                  : "Read the instructions for at least ${secondsLeft == 0 ? '$minutesLeft minute${minutesLeft > 1 ? 's' : ''}' : minutesLeft == 0 ? '$secondsLeft second${secondsLeft > 1 ? 's' : ''}' : '$minutesLeft minute${minutesLeft > 1 ? 's' : ''} and $secondsLeft second${secondsLeft > 1 ? 's' : ''}'}"
          ),
        )
    );
  });
}