

import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:url_launcher/url_launcher.dart';

import '../util/checks.dart';
import '../widget/shared/setting_tile.dart';

class SettingsPage extends StatelessWidget {
  final GameController _gameController  = Get.find<GameController>();
  final SettingsController _settingsController = Get.find<SettingsController>();

  SettingsPage({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) => Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        SettingTile(
            title: "File settings",
            subtitle: "This section contains all the settings related to files used by Fortnite",
            expandedContent: [
              _createFileSetting(
                title: "Game server",
                description: "This file is injected to create a game server to host matches",
                controller: _settingsController.rebootDll
              ),
              _createFileSetting(
                  title: "Unreal engine console",
                  description: "This file is injected to unlock the Unreal Engine Console in-game",
                  controller: _settingsController.consoleDll
              ),
              _createFileSetting(
                  title: "Authentication patcher",
                  description: "This file is injected to redirect all HTTP requests to the local backend",
                  controller: _settingsController.authDll
              ),
            ],
        ),
        const SizedBox(
          height: 16.0,
        ),
        SettingTile(
            title: "Automatic updates",
            subtitle: "Choose whether the launcher and its files should be automatically updated",
            contentWidth: null,
            content: Obx(() => ToggleSwitch(
                checked: _settingsController.autoUpdate(),
                onChanged: (value) => _settingsController.autoUpdate.value = value
            ))
        ),
        const SizedBox(
          height: 16.0,
        ),
        SettingTile(
            title: "Custom launch arguments",
            subtitle: "Enter additional arguments to use when launching the game",
            content: TextFormBox(
                placeholder: "args",
              controller: _gameController.customLaunchArgs,
            )
        ),
        const SizedBox(
          height: 16.0,
        ),
        SettingTile(
            title: "Create a bug report",
            subtitle: "Help me fix bugs by reporting them",
            content: Button(
              onPressed: () => launchUrl(Uri.parse("https://discord.com/channels/998020695223193670/1031262639457828910")),
              child: const Text("Report a bug"),
            )
        ),
      ]
  );

  Widget _createFileSetting({required String title, required String description, required TextEditingController controller}) => ListTile(
      title: Text(title),
      subtitle: Text(description),
      trailing: SizedBox(
          width: 256,
          child: Row(
            children: [
              Expanded(
                child: TextFormBox(
                    placeholder: "path",
                    controller: controller,
                    validator: checkDll,
                    autovalidateMode: AutovalidateMode.always
                ),
              ),
              const SizedBox(
                width: 8.0,
              ),
              Padding(
                padding: const EdgeInsets.only(bottom: 21.0),
                child: Button(
                  onPressed: () {  },
                  child: const Icon(FluentIcons.open_folder_horizontal),
                ),
              )
            ],
          )
      )
  );
}
