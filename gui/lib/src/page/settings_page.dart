import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/services.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/build_controller.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/hosting_controller.dart';
import 'package:reboot_launcher/src/controller/authenticator_controller.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/controller/update_controller.dart';
import 'package:reboot_launcher/src/dialog/abstract/dialog_button.dart';
import 'package:reboot_launcher/src/widget/common/file_selector.dart';
import 'package:url_launcher/url_launcher.dart';

import 'package:reboot_launcher/src/util/checks.dart';
import 'package:reboot_launcher/src/dialog/abstract/dialog.dart';
import 'package:reboot_launcher/src/widget/common/setting_tile.dart';

class SettingsPage extends StatefulWidget {
  const SettingsPage({Key? key}) : super(key: key);

  @override
  State<SettingsPage> createState() => _SettingsPageState();
}

class _SettingsPageState extends State<SettingsPage> with AutomaticKeepAliveClientMixin {
  final BuildController _buildController = Get.find<BuildController>();
  final GameController _gameController  = Get.find<GameController>();
  final HostingController _hostingController = Get.find<HostingController>();
  final AuthenticatorController _authenticatorController = Get.find<AuthenticatorController>();
  final SettingsController _settingsController = Get.find<SettingsController>();
  final UpdateController _updateController = Get.find<UpdateController>();

  @override
  bool get wantKeepAlive => true;

  @override
  Widget build(BuildContext context) {
    super.build(context);
    return ListView(
        children: [
          SettingTile(
            title: "Client settings",
            subtitle: "This section contains the dlls used to make the Fortnite client work",
            expandedContent: [
              _createFileSetting(
                  title: "Unreal engine console",
                  description: "This file is injected to unlock the Unreal Engine Console",
                  controller: _settingsController.unrealEngineConsoleDll
              ),
              _createFileSetting(
                  title: "Authentication patcher",
                  description: "This file is injected to redirect all HTTP requests to the launcher's authenticator",
                  controller: _settingsController.authenticatorDll
              ),
              SettingTile(
                  title: "Custom launch arguments",
                  subtitle: "Additional arguments to use when launching the game",
                  isChild: true,
                  content: TextFormBox(
                    placeholder: "Arguments...",
                    controller: _gameController.customLaunchArgs,
                  )
              ),
            ],
          ),
          const SizedBox(
            height: 8.0,
          ),
          SettingTile(
            title: "Game server settings",
            subtitle: "This section contains settings related to the game server implementation",
            expandedContent: [
              _createFileSetting(
                  title: "Implementation",
                  description: "This file is injected to create a game server & host matches",
                  controller: _settingsController.gameServerDll
              ),
              SettingTile(
                  title: "Port",
                  subtitle: "The port used by the game server dll",
                  content: TextFormBox(
                      placeholder: "Port",
                      controller: _settingsController.gameServerPort,
                      keyboardType: TextInputType.number,
                      inputFormatters: [
                        FilteringTextInputFormatter.digitsOnly
                      ]
                  ),
                  isChild: true
              ),
              SettingTile(
                  title: "Update mirror",
                  subtitle: "The URL used to update the game server dll",
                  content: TextFormBox(
                      placeholder: "URL",
                      controller: _updateController.url,
                      validator: checkUpdateUrl
                  ),
                  isChild: true
              ),
              SettingTile(
                  title: "Update timer",
                  subtitle: "Determines when the game server dll should be updated",
                  content: Obx(() => DropDownButton(
                      leading: Text(_updateController.timer.value.text),
                      items: UpdateTimer.values.map((entry) => MenuFlyoutItem(
                          text: Text(entry.text),
                          onPressed: () {
                            _updateController.timer.value = entry;
                            _updateController.update(true);
                          }
                      )).toList()
                  )),
                  isChild: true
              ),
            ],
          ),
          const SizedBox(
            height: 8.0,
          ),
          SettingTile(
            title: "Launcher utilities",
            subtitle: "This section contains handy settings for the launcher",
            expandedContent: [
              SettingTile(
                  title: "Installation directory",
                  subtitle: "Opens the installation directory",
                  isChild: true,
                  content: Button(
                    onPressed: () => launchUrl(installationDirectory.uri),
                    child: const Text("Show Files"),
                  )
              ),
              SettingTile(
                  title: "Create a bug report",
                  subtitle: "Help me fix bugs by reporting them",
                  isChild: true,
                  content: Button(
                    onPressed: () => launchUrl(Uri.parse("https://github.com/Auties00/reboot_launcher/issues")),
                    child: const Text("Report a bug"),
                  )
              ),
              SettingTile(
                  title: "Reset settings",
                  subtitle: "Resets the launcher's settings to their default values",
                  isChild: true,
                  content: Button(
                    onPressed: () => showAppDialog(
                        builder: (context) => InfoDialog(
                          text: "Do you want to reset all the launcher's settings to their default values? This action is irreversible",
                          buttons: [
                            DialogButton(
                              type: ButtonType.secondary,
                              text: "Close",
                            ),
                            DialogButton(
                              type: ButtonType.primary,
                              text: "Reset",
                              onTap: () {
                                _buildController.reset();
                                _gameController.reset();
                                _hostingController.reset();
                                _authenticatorController.reset();
                                _settingsController.reset();
                                _updateController.reset();
                                Navigator.of(context).pop();
                              },
                            )
                          ],
                        )
                    ),
                    child: const Text("Reset"),
                  )
              )
            ],
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

extension _UpdateTimerExtension on UpdateTimer {
  String get text => this == UpdateTimer.never ? "Never" : "Every $name";
}