import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';
import 'package:reboot_launcher/src/model/server_type.dart';
import 'package:reboot_launcher/src/widget/shared/smart_input.dart';

class HostInput extends StatelessWidget {
  final ServerController _serverController = Get.find<ServerController>();

  HostInput({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return Tooltip(
        message: "The hostname of the lawin server",
        child: Obx(() => SmartInput(
          label: "Host",
          placeholder: "Type the lawin server's hostname",
          controller: _serverController.host,
          enabled: _serverController.type.value == ServerType.remote
        ))
    );
  }
}
