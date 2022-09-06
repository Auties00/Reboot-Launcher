import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/widget/smart_switch.dart';

import 'package:reboot_launcher/src/controller/server_controller.dart';

class LocalServerSwitch extends StatelessWidget {
  final ServerController _serverController = Get.find<ServerController>();

  LocalServerSwitch({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return SmartSwitch(
        value: _serverController.embedded,
        label: "Embedded"
    );
  }
}
