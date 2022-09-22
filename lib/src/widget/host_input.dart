import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/widget/smart_input.dart';

import 'package:reboot_launcher/src/controller/server_controller.dart';

class HostInput extends StatelessWidget {
  final ServerController _serverController = Get.find<ServerController>();

  HostInput({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return Obx(() => Tooltip(
          message: _serverController.embedded.value
              ? "The remote lawin host cannot be set when running on embedded"
              : "The remote host of the lawin server to use for authentication",
          child: _buildInput(context),
        ));
  }

  SmartInput _buildInput(BuildContext context) {
    return SmartInput(
      label: "Host",
      placeholder: "Type the host name",
      controller: _serverController.host,
      enabled: !_serverController.embedded.value,
      onTap: () => _serverController.embedded.value
          ? showSnackbar(
              context,
              const Snackbar(
                  content: Text("The host is locked when embedded is on")))
          : {},
    );
  }
}
