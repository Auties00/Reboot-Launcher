import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';
import 'package:reboot_launcher/src/controller/warning_controller.dart';
import 'package:reboot_launcher/src/widget/local_server_switch.dart';
import 'package:reboot_launcher/src/widget/port_input.dart';

import 'package:reboot_launcher/src/widget/host_input.dart';
import 'package:reboot_launcher/src/widget/server_button.dart';

import 'package:reboot_launcher/src/widget/restart_warning.dart';

class ServerPage extends StatelessWidget {
  final WarningController _warningController = Get.find<WarningController>();

  ServerPage({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return Obx(() => Column(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              if (_warningController.warning.value) const RestartWarning(),
              HostInput(),
              PortInput(),
              LocalServerSwitch(),
              ServerButton()
            ]));
  }
}
