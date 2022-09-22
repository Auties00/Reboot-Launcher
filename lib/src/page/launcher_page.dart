import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/widget/deployment_selector.dart';
import 'package:reboot_launcher/src/widget/launch_button.dart';
import 'package:reboot_launcher/src/widget/username_box.dart';

import 'package:reboot_launcher/src/widget/version_selector.dart';

class LauncherPage extends StatelessWidget {
  const LauncherPage({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return Column(
      mainAxisAlignment: MainAxisAlignment.spaceBetween,
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        UsernameBox(),
        VersionSelector(),
        DeploymentSelector(enabled: false),
        const LaunchButton()
      ],
    );
  }
}
