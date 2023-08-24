import 'dart:async';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get/get_rx/src/rx_types/rx_types.dart';

import 'package:reboot_launcher/src/util/checks.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/ui/controller/game_controller.dart';
import 'package:reboot_launcher/src/ui/controller/settings_controller.dart';
import 'package:reboot_launcher/src/ui/dialog/snackbar.dart';
import 'package:reboot_launcher/src/ui/widget/home/launch_button.dart';
import 'package:reboot_launcher/src/ui/widget/home/setting_tile.dart';
import 'package:reboot_launcher/src/ui/widget/home/version_selector.dart';

class PlayPage extends StatefulWidget {
  final GlobalKey<NavigatorState> navigatorKey;
  final RxInt nestedNavigation;
  const PlayPage(this.navigatorKey, this.nestedNavigation, {Key? key}) : super(key: key);

  @override
  State<PlayPage> createState() => _PlayPageState();
}

class _PlayPageState extends State<PlayPage> {
  final GameController _gameController = Get.find<GameController>();
  final SettingsController _settingsController = Get.find<SettingsController>();
  final StreamController _matchmakingStream = StreamController();

  @override
  void initState() {
    _gameController.password.addListener(() => _matchmakingStream.add(null));
    _settingsController.matchmakingIp.addListener(() =>
        _matchmakingStream.add(null));
    super.initState();
  }

  @override
  Widget build(BuildContext context) => Column(
    children: [
      Expanded(
        child: ListView(
          children: [
            SettingTile(
                title: "Version",
                subtitle: "Select the version of Fortnite you want to play",
                content: const VersionSelector(),
                expandedContent: [
                  SettingTile(
                      title: "Add a version from this PC's local storage",
                      subtitle: "Versions coming from your local disk are not guaranteed to work",
                      content: Button(
                        onPressed: () =>
                            VersionSelector.openAddDialog(context),
                        child: const Text("Add build"),
                      ),
                      isChild: true
                  ),
                  SettingTile(
                      title: "Download any version from the cloud",
                      subtitle: "A curated list of supported versions by Project Reboot",
                      content: Button(
                        onPressed: () =>
                            VersionSelector.openDownloadDialog(context),
                        child: const Text("Download"),
                      ),
                      isChild: true
                  )
                ]
            ),
            const SizedBox(
              height: 8.0,
            ),
            StreamBuilder(
                stream: _matchmakingStream.stream,
                builder: (context, value) =>
                    SettingTile(
                        title: "Matchmaking host",
                        subtitle: "Enter the IP address of the game server hosting the match",
                        content: TextFormBox(
                            placeholder: "IP:PORT",
                            controller: _settingsController.matchmakingIp,
                            validator: checkMatchmaking,
                            autovalidateMode: AutovalidateMode.always
                        ),
                        expandedContent: [
                          SettingTile(
                              title: "Automatically start game server",
                              subtitle: "This option is available when the matchmaker is set to localhost",
                              contentWidth: null,
                              content: !isLocalHost(
                                  _settingsController.matchmakingIp.text) ||
                                  _gameController.password.text.isNotEmpty
                                  ? _disabledAutoGameServerSwitch
                                  : _autoGameServerSwitch,
                              isChild: true
                          ),
                          SettingTile(
                              title: "Browse available servers",
                              subtitle: "Discover new game servers that fit your play-style",
                              content: Button(
                                  onPressed: () {
                                    widget.navigatorKey.currentState
                                        ?.pushNamed('browse');
                                    widget.nestedNavigation.value += 1;
                                  },
                                  child: const Text("Browse")
                              ),
                              isChild: true
                          )
                        ]
                    )
            ),
          ],
        ),
      ),
      const SizedBox(
        height: 8.0,
      ),
      const LaunchButton(
          host: false
      )
    ],
  );

  Widget get _disabledAutoGameServerSwitch => Container(
    foregroundDecoration: const BoxDecoration(
      color: Colors.grey,
      backgroundBlendMode: BlendMode.saturation,
    ),
    child: _autoGameServerSwitch,
  );

  Widget get _autoGameServerSwitch => Obx(() => ToggleSwitch(
      checked: _gameController.autoStartGameServer() &&
          isLocalHost(_settingsController.matchmakingIp.text) &&
          _gameController.password.text.isEmpty,
      onChanged: (value) {
        if (!isLocalHost(_settingsController.matchmakingIp.text)) {
          showMessage(
              "This option isn't available when the matchmaker isn't set to 127.0.0.1");
          return;
        }

        if (_gameController.password.text.isNotEmpty) {
          showMessage(
              "This option isn't available when the password isn't empty(LawinV2)");
          return;
        }

        _gameController.autoStartGameServer.value = value;
      }
  ));
}