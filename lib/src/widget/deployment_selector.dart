import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/widget/smart_switch.dart';

import '../util/generic_controller.dart';

class DeploymentSelector extends StatelessWidget {
  final GenericController<bool> controller;
  final VoidCallback onSelected;
  final bool enabled;

  const DeploymentSelector(
      {Key? key,
      required this.controller,
      required this.onSelected,
      required this.enabled})
      : super(key: key);

  @override
  Widget build(BuildContext context) {
    return SmartSwitch(
        onDisabledPress: !enabled
            ? () => showSnackbar(context,
                const Snackbar(content: Text("Hosting is not allowed")))
            : null,
        keyName: "reboot",
        label: "Host",
        controller: controller,
        onSelected: _onSelected,
        enabled: enabled);
  }

  void _onSelected(bool value) {
    controller.value = value;
    onSelected();
  }
}
