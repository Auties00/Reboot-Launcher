import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';
import 'package:reboot_launcher/src/widget/server/host_input.dart';
import 'package:reboot_launcher/src/widget/server/server_type_selector.dart';
import 'package:reboot_launcher/src/widget/server/port_input.dart';
import 'package:reboot_launcher/src/widget/server/server_button.dart';

import '../model/server_type.dart';
import '../widget/shared/setting_tile.dart';

class ServerPage extends StatelessWidget {
  final ServerController _serverController = Get.find<ServerController>();

  ServerPage({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return Obx(() => Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const SizedBox(
            width: double.infinity,
            child: InfoBar(
                title: Text("The backend server handles authentication and parties, not game hosting"),
                severity: InfoBarSeverity.info
            ),
          ),
          const SizedBox(
            height: 16.0,
          ),
          SettingTile(
              title: "Host",
              subtitle: "Enter the host of the backend server",
              content: TextFormBox(
                  placeholder: "host",
                  controller: _serverController.host,
                  enabled: _isRemote
              )
          ),
          const SizedBox(
            height: 16.0,
          ),
          SettingTile(
              title: "Host",
              subtitle: "Enter the port of the backend server",
              content: TextFormBox(
                  placeholder: "host",
                  controller: _serverController.port,
                  enabled: _isRemote
              )
          ),
          const SizedBox(
            height: 16.0,
          ),
          SettingTile(
              title: "Type",
              subtitle: "Select the type of backend to use",
              content: ServerTypeSelector()
          ),
          const SizedBox(
            height: 16.0,
          ),
          Align(
            alignment: Alignment.bottomCenter,
            child: SettingTile(
              title: "Login automatically",
              subtitle: "Choose whether the game client should login automatically using random credentials",
              contentWidth: null,
              content: Obx(() => ToggleSwitch(
                  checked: _serverController.loginAutomatically(),
                  onChanged: (value) => _serverController.loginAutomatically.value = value
              ))
            ),
          ),
          const Expanded(child: SizedBox()),
          const ServerButton()
        ]
    ));
  }

  bool get _isRemote => _serverController.type.value == ServerType.remote;
}
