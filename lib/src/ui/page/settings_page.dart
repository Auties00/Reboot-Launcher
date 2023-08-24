import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/ui/controller/game_controller.dart';
import 'package:reboot_launcher/src/ui/controller/settings_controller.dart';
import 'package:reboot_launcher/src/ui/dialog/dialog_button.dart';
import 'package:reboot_launcher/src/ui/widget/shared/file_selector.dart';
import 'package:url_launcher/url_launcher.dart';

import 'package:reboot_launcher/src/util/checks.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/ui/dialog/dialog.dart';
import 'package:reboot_launcher/src/ui/widget/home/setting_tile.dart';

class SettingsPage extends StatefulWidget {
  const SettingsPage({Key? key}) : super(key: key);

  @override
  State<SettingsPage> createState() => _SettingsPageState();
}

class _SettingsPageState extends State<SettingsPage> with AutomaticKeepAliveClientMixin {
  final GameController _gameController  = Get.find<GameController>();

  final SettingsController _settingsController = Get.find<SettingsController>();

  @override
  bool get wantKeepAlive => true;

  @override
  Widget build(BuildContext context) {
    super.build(context);
    return ListView(
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
            height: 8.0,
          ),
          SettingTile(
              title: "Automatic updates",
              subtitle: "Choose whether the launcher and its files should be automatically updated",
              contentWidth: null,
              content: Obx(() => ToggleSwitch(
                  checked: _settingsController.autoUpdate.value,
                  onChanged: (value) => _settingsController.autoUpdate.value = value
              )),
              expandedContentSpacing: 0,
              expandedContent: [
                SettingTile(
                    title: "Update Mirror",
                    subtitle: "The URL used to pull the latest update once a day",
                    content: Obx(() => TextFormBox(
                        placeholder: "URL",
                        controller: _settingsController.updateUrl,
                        enabled: _settingsController.autoUpdate.value,
                        validator: checkUpdateUrl
                    )),
                    isChild: true
                )
              ]
          ),
          const SizedBox(
            height: 8.0,
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
            height: 8.0,
          ),
          SettingTile(
              title: "Create a bug report",
              subtitle: "Help me fix bugs by reporting them",
              content: Button(
                onPressed: () => launchUrl(Uri.parse("https://github.com/Auties00/reboot_launcher/issues")),
                child: const Text("Report a bug"),
              )
          ),
          const SizedBox(
            height: 8.0,
          ),
          SettingTile(
              title: "Reset settings",
              subtitle: "Resets the launcher's settings to their default values",
              content: Button(
                onPressed: () => showDialog(
                    context: context,
                    builder: (context) => InfoDialog(
                      text: "Do you want to reset all the setting in this tab to their default values? This action is irreversible",
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
            height: 8.0,
          ),
          SettingTile(
              title: "Version status",
              subtitle: "Current version: 8.1",
              content: Button(
                onPressed: () => launchUrl(installationDirectory.uri),
                child: const Text("Show Files"),
              )
          ),
        ]
    );
  }

  Widget _createFileSetting({required String title, required String description, required TextEditingController controller}) => SettingTile(
      title: title,
      subtitle: description,
      content: FileSelector(
          placeholder: "Path",
          windowTitle: "Select a file",
          controller: controller,
          validator: checkDll,
          extension: "dll",
          folder: false
      ),
      isChild: true
  );
}
