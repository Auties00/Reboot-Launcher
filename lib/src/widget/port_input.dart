import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/widget/smart_input.dart';

import '../util/generic_controller.dart';

class PortInput extends StatelessWidget {
  final TextEditingController controller;
  final GenericController<bool> localController;

  const PortInput({
    Key? key,
    required this.controller,
    required this.localController
  }) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return SmartInput(
      keyName: "port",
      label: "Port",
      placeholder: "Type the host port",
      controller: controller,
      enabled: !localController.value,
      onTap: () => localController.value
          ? showSnackbar(context, const Snackbar(content: Text("The port is locked when embedded is on")))
          : {},
    );
  }
}