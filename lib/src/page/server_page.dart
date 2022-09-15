import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/widget/local_server_switch.dart';
import 'package:reboot_launcher/src/widget/port_input.dart';

import 'package:reboot_launcher/src/widget/host_input.dart';
import 'package:reboot_launcher/src/widget/server_button.dart';

class ServerPage extends StatelessWidget {
  const ServerPage({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return Column(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          HostInput(),
          PortInput(),
          LocalServerSwitch(),
          ServerButton()
        ]);
  }
}
