import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/widget/smart_switch.dart';

import '../util/generic_controller.dart';

class LocalServerSwitch extends StatelessWidget {
  final GenericController<bool> controller;
  final Function(bool)? onSelected;

  const LocalServerSwitch({Key? key, required this.controller, this.onSelected})
      : super(key: key);

  @override
  Widget build(BuildContext context) {
    return SmartSwitch(
      keyName: "local",
      label: "Embedded",
      controller: controller,
      onSelected: onSelected
    );
  }
}
