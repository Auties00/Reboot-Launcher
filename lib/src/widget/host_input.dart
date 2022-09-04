import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/widget/smart_input.dart';

import '../util/generic_controller.dart';

class HostInput extends StatelessWidget {
  final TextEditingController controller;
  final GenericController<bool> localController;

  const HostInput(
      {Key? key, required this.controller, required this.localController})
      : super(key: key);

  @override
  Widget build(BuildContext context) {
    return SmartInput(
      keyName: "host",
      label: "Host",
      placeholder: "Type the host name",
      controller: controller,
      enabled: !localController.value,
      onTap: () => localController.value
          ? showSnackbar(context, const Snackbar(content: Text("The host is locked when embedded is on")))
          : {},
    );
  }
}
