import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';
import 'package:reboot_launcher/src/widget/host_input.dart';
import 'package:reboot_launcher/src/widget/local_server_switch.dart';
import 'package:reboot_launcher/src/widget/port_input.dart';
import 'package:reboot_launcher/src/widget/server_button.dart';
import 'package:reboot_launcher/src/widget/warning_info.dart';

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
            LocalServerSwitch(),
            ServerButton()
          ]
      )),
    );
  }
}
