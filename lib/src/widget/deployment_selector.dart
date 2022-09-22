import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/widget/smart_switch.dart';

import 'package:reboot_launcher/src/controller/game_controller.dart';

class DeploymentSelector extends StatelessWidget {
  final GameController _gameController = Get.find<GameController>();
  final bool enabled;

  DeploymentSelector({Key? key, required this.enabled}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return Tooltip(
      message: enabled ? "Whether the launched client should be used to host multiplayer games or not" : "Hosting is not allowed",
      child: _buildSwitch(context)
    );
  }

  SmartSwitch _buildSwitch(BuildContext context) {
    return SmartSwitch(
      value: _gameController.host,
      onDisabledPress: !enabled
          ? () => showSnackbar(context,
              const Snackbar(content: Text("Hosting is not allowed")))
          : null,
      label: "Host",
      enabled: enabled
  );
  }
}
