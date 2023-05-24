import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/ui/controller/game_controller.dart';
import 'package:reboot_launcher/src/ui/controller/settings_controller.dart';
import 'package:reboot_launcher/src/ui/dialog/dialog_button.dart';
import 'package:url_launcher/url_launcher.dart';


import '../../util/checks.dart';
import '../../util/os.dart';
import '../../util/selector.dart';
import '../dialog/dialog.dart';
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
              placeholder: "Arguments...",
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
              onPressed: () => launchUrl(Uri.parse("https://github.com/Auties00/reboot_launcher/issues/new/choose")),
              child: const Text("Report a bug"),
            )
        ),
        const SizedBox(
          height: 16.0,
        ),
        SettingTile(
            title: "Reset settings",
            subtitle: "Resets the launcher's settings to their default values",
            content: Button(
              onPressed: () => showDialog(
                  context: context,
                  builder: (context) => InfoDialog(
                    text: "Do you want to reset all settings to their default values? This action is irreversible",
                    buttons: [
                      DialogButton(
                        type: ButtonType.secondary,
                        text: "Close",
                      ),
                      DialogButton(
                        type: ButtonType.primary,
                        text: "Reset",
                        onTap: () {
                          _settingsController.reset();
                          Navigator.of(context).pop();
                        },
                      )
                    ],
                  )
              ),
              child: const Text("Reset"),
            )
        ),
        const SizedBox(
          height: 16.0,
        ),
        SettingTile(
            title: "Version status",
            subtitle: "Current version: 7.0",
            content: Button(
              onPressed: () => launchUrl(installationDirectory.uri),
              child: const Text("Show Files"),
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
                    placeholder: "Path",
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
                  onPressed: () async {
                    var selected = await compute(openFilePicker, "dll");
                    controller.text = selected ?? controller.text;
                  },
                  child: const Icon(FluentIcons.open_folder_horizontal),
                ),
              )
            ],
          )
      )
  );
}
