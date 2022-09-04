import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/widget/smart_input.dart';

import '../util/generic_controller.dart';

class UsernameBox extends StatelessWidget {
  final TextEditingController controller;
  final GenericController<bool> rebootController;

  const UsernameBox({Key? key, required this.controller, required this.rebootController}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return SmartInput(
      keyName: "${rebootController.value ? 'host' : 'game'}_username",
      label: "Username",
      placeholder: "Type your ${rebootController.value ? 'hosting' : "in-game"} username",
      controller: controller,
      populate: true
    );
  }
}
