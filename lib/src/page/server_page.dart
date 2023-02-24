import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';
import 'package:reboot_launcher/src/widget/server/host_input.dart';
import 'package:reboot_launcher/src/widget/server/server_type_selector.dart';
import 'package:reboot_launcher/src/widget/server/port_input.dart';
import 'package:reboot_launcher/src/widget/server/server_button.dart';

class ServerPage extends StatelessWidget {
  final ServerController _serverController = Get.find<ServerController>();

  ServerPage({Key? key}) : super(key: key);
  
  @override
  Widget build(BuildContext context) {
    return Obx(() => Column(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          if(_serverController.warning.value)
            GestureDetector(
              onTap: () => _serverController.warning.value = false,
              child: const MouseRegion(
                cursor: SystemMouseCursors.click,
                child: SizedBox(
                  width: double.infinity,
                  child: InfoBar(
                      title: Text("The backend server handles authentication and parties, not game hosting"),
                      severity: InfoBarSeverity.info
                  ),
                ),
              ),
            ),
          HostInput(),
          PortInput(),
          ServerTypeSelector(),
          const ServerButton()
        ]
    ));
  }
}
