import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';
import 'package:reboot_launcher/src/widget/server/host_input.dart';
import 'package:reboot_launcher/src/widget/server/server_type_selector.dart';
import 'package:reboot_launcher/src/widget/server/port_input.dart';
import 'package:reboot_launcher/src/widget/server/server_button.dart';
import 'package:reboot_launcher/src/widget/shared/warning_info.dart';

class ServerPage extends StatelessWidget {
  final ServerController _serverController = Get.find<ServerController>();

  ServerPage({Key? key}) : super(key: key);
  
  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.all(12.0),
      child: Obx(() => Column(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            if(_serverController.warning.value)
              WarningInfo(
                  text: "The lawin server handles authentication and parties, not game hosting",
                  icon: FluentIcons.accept,
                  onPressed: () => _serverController.warning.value = false
              ),
            HostInput(),
            PortInput(),
            ServerTypeSelector(),
            const ServerButton()
          ]
      )),
    );
  }
}
