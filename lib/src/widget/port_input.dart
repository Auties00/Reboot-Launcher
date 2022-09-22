import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/widget/smart_input.dart';

import 'package:reboot_launcher/src/controller/server_controller.dart';

class PortInput extends StatelessWidget {
  final ServerController _serverController = Get.find<ServerController>();

  PortInput({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return Obx(() => Tooltip(
        message: _serverController.embedded.value
            ? "The remote lawin port cannot be set when running on embedded"
            : "The remote port of the lawin server to use for authentication",
        child: _buildInput(context)));
  }

  SmartInput _buildInput(BuildContext context) {
    return SmartInput(
      label: "Port",
      placeholder: "Type the host port",
      controller: _serverController.port,
      enabled: !_serverController.embedded.value,
      onTap: () => _serverController.embedded.value
          ? showSnackbar(
              context,
              const Snackbar(
                  content: Text("The port is locked when embedded is on")))
          : {},
    );
  }
}
