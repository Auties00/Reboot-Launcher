import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/model/server_type.dart';
import 'package:reboot_launcher/src/util/server.dart';
import 'package:reboot_launcher/src/ui/controller/server_controller.dart';
import 'package:reboot_launcher/src/ui/dialog/snackbar.dart';
import 'package:reboot_launcher/src/ui/widget/server/server_type_selector.dart';
import 'package:reboot_launcher/src/ui/widget/server/server_button.dart';
import 'package:url_launcher/url_launcher.dart';

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
                  placeholder: "Host",
                  controller: _serverController.host,
                  enabled: _isRemote
              )
          ),
          const SizedBox(
            height: 16.0,
          ),
          SettingTile(
              title: "Port",
              subtitle: "Enter the port of the backend server",
              content: TextFormBox(
                  placeholder: "Port",
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
          SettingTile(
              title: "Detached",
              subtitle: "Choose whether the backend should be started as a separate process, useful for debugging",
              contentWidth: null,
              content: Obx(() => ToggleSwitch(
                  checked: _serverController.detached(),
                  onChanged: (value) => _serverController.detached.value = value
              ))
          ),
          const SizedBox(
            height: 16.0,
          ),
          SettingTile(
              title: "Server files",
              subtitle: "The location where the backend is stored",
              content: Button(
                  onPressed: () => launchUrl(serverDirectory.uri),
                  child: const Text("Open")
              )
          ),
          const Expanded(child: SizedBox()),
          const ServerButton()
        ]
    ));
  }

  bool get _isRemote => _serverController.type.value == ServerType.remote;
}
