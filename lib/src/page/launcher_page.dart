import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/widget/deployment_selector.dart';
import 'package:reboot_launcher/src/widget/launch_button.dart';
import 'package:reboot_launcher/src/widget/restart_warning.dart';
import 'package:reboot_launcher/src/widget/username_box.dart';

import 'package:reboot_launcher/src/widget/version_selector.dart';

import 'package:reboot_launcher/src/controller/warning_controller.dart';

class LauncherPage extends StatelessWidget {
  final WarningController _warningController = Get.find<WarningController>();

  LauncherPage({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return Obx(() => Column(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            if (_warningController.warning.value) const RestartWarning(),
            UsernameBox(),
            VersionSelector(),
            DeploymentSelector(enabled: true),
            const LaunchButton()
          ],
        ));
  }
}
