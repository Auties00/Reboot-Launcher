import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';
import 'package:reboot_launcher/src/model/server_type.dart';
import 'package:reboot_launcher/src/widget/shared/smart_input.dart';


class PortInput extends StatelessWidget {
  final ServerController _serverController = Get.find<ServerController>();

  PortInput({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return Tooltip(
        message: "The port of the lawin server",
        child: Obx(() => SmartInput(
            label: "Port",
            placeholder: "Type the lawin server's port",
            controller: _serverController.port,
            enabled: _serverController.type.value != ServerType.embedded
        ))
    );
  }
}
