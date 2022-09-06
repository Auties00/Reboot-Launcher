import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/widget/smart_input.dart';

import 'package:reboot_launcher/src/controller/server_controller.dart';

class HostInput extends StatelessWidget {
  final ServerController _serverController = Get.put(ServerController());

  HostInput({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return Obx(() => SmartInput(
          keyName: "host",
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
        ));
  }
}
