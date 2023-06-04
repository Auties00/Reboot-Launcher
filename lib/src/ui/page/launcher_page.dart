

import 'dart:async';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/material.dart' show Icons;
import 'package:get/get.dart';
import 'package:reboot_launcher/src/ui/controller/game_controller.dart';
import 'package:reboot_launcher/src/ui/controller/settings_controller.dart';
import 'package:reboot_launcher/src/ui/dialog/snackbar.dart';
import 'package:reboot_launcher/src/ui/page/browse_page.dart';
import 'package:reboot_launcher/src/ui/widget/home/launch_button.dart';
import 'package:reboot_launcher/src/ui/widget/home/version_selector.dart';
import 'package:reboot_launcher/src/ui/widget/shared/setting_tile.dart';

import '../../util/checks.dart';
import '../../util/os.dart';

class LauncherPage extends StatefulWidget {
  final GlobalKey<NavigatorState> navigatorKey;
  final RxBool nestedNavigation;
  const LauncherPage(this.navigatorKey, this.nestedNavigation, {Key? key}) : super(key: key);

  @override
  State<LauncherPage> createState() => _LauncherPageState();
}

class _LauncherPageState extends State<LauncherPage> with AutomaticKeepAliveClientMixin {
  @override
  bool get wantKeepAlive => true;

  @override
  Widget build(BuildContext context) {
    super.build(context);
    return Navigator(
      key: widget.navigatorKey,
      initialRoute: "home",
      onGenerateRoute: (settings) {
        var screen = _createScreen(settings.name);
        return FluentPageRoute(
            builder: (context) => screen,
            settings: settings
        );
      },
    );
  }

  Widget _createScreen(String? name) {
    switch(name){
      case "home":
        return _GamePage(widget.navigatorKey, widget.nestedNavigation);
      case "browse":
        return const BrowsePage();
      default:
        throw Exception("Unknown page: $name");
    }
  }
}

class _GamePage extends StatefulWidget {
  final GlobalKey<NavigatorState> navigatorKey;
  final RxBool nestedNavigation;
  const _GamePage(this.navigatorKey, this.nestedNavigation, {Key? key}) : super(key: key);

  @override
  State<_GamePage> createState() => _GamePageState();
}

class _GamePageState extends State<_GamePage> {
  final GameController _gameController = Get.find<GameController>();
  final SettingsController _settingsController = Get.find<SettingsController>();
  late final RxBool _showPasswordTrailing = RxBool(_gameController.password.text.isNotEmpty);
  final StreamController _matchmakingStream = StreamController();

  @override
  void initState() {
    _gameController.password.addListener(() => _matchmakingStream.add(null));
    _settingsController.matchmakingIp.addListener(() => _matchmakingStream.add(null));
    super.initState();
  }

  @override
  Widget build(BuildContext context) => Column(
    children: [
      Expanded(
        child: ListView(
          children: [
            SettingTile(
              title: "Credentials",
              subtitle: "Your in-game login credentials",
              expandedContentSpacing: 0,
              expandedContent: [
                SettingTile(
                  title: "Username",
                  subtitle: "The username that other players will see when you are in game",
                  isChild: true,
                  content: TextFormBox(
                      placeholder: "Username",
                      controller: _gameController.username,
                      autovalidateMode: AutovalidateMode.always
                  ),
                ),
                SettingTile(
                    title: "Password",
                    subtitle: "The password of your account, only used if the backend requires it",
                    isChild: true,
                    content: Obx(() => TextFormBox(
                        placeholder: "Password",
                        controller: _gameController.password,
                        autovalidateMode: AutovalidateMode.always,
                        obscureText: !_gameController.showPassword.value,
                        enableSuggestions: false,
                        autocorrect: false,
                        onChanged: (text) => _showPasswordTrailing.value = text.isNotEmpty,
                        suffix: Button(
                          onPressed: () => _gameController.showPassword.value = !_gameController.showPassword.value,
                          style: ButtonStyle(
                              shape: ButtonState.all(const CircleBorder()),
                              backgroundColor: ButtonState.all(Colors.transparent)
                          ),
                          child: Icon(
                              _gameController.showPassword.value ? Icons.visibility_off : Icons.visibility,
                              color: _showPasswordTrailing.value ? null : Colors.transparent
                          ),
                        )
                    ))
                )
              ],
            ),
            const SizedBox(
              height: 16.0,
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
                              content: Obx(() => !isLocalHost(_settingsController.matchmakingIp.text) || _gameController.password.text.isNotEmpty ?  Container(
                                foregroundDecoration: const BoxDecoration(
                                  color: Colors.grey,
                                  backgroundBlendMode: BlendMode.saturation,
                                ),
                                child: _autoGameServerSwitch,
                              ) : _autoGameServerSwitch),
                              isChild: true
                          ),
                          SettingTile(
                              title: "Browse available servers",
                              subtitle: "Discover new game servers that fit your play-style",
                              content: Button(
                                  onPressed: () {
                                    widget.navigatorKey.currentState?.pushNamed('browse');
                                    widget.nestedNavigation.value = true;
                                  },
                                  child: const Text("Browse")
                              ),
                              isChild: true
                          )
                        ]
                    )
            ),
            const SizedBox(
              height: 16.0,
            ),
            SettingTile(
                title: "Version",
                subtitle: "Select the version of Fortnite you want to play",
                content: const VersionSelector(),
                expandedContent: [
                  SettingTile(
                      title: "Add a version from this PC's local storage",
                      subtitle: "Versions coming from your local disk are not guaranteed to work",
                      content: Button(
                        onPressed: () => VersionSelector.openAddDialog(context),
                        child: const Text("Add build"),
                      ),
                      isChild: true
                  ),
                  SettingTile(
                      title: "Download any version from the cloud",
                      subtitle: "A curated list of supported versions by Project Reboot",
                      content: Button(
                        onPressed: () => VersionSelector.openDownloadDialog(context),
                        child: const Text("Download"),
                      ),
                      isChild: true
                  )
                ]
            )
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

  ToggleSwitch get _autoGameServerSwitch => ToggleSwitch(
      checked: isLocalHost(_settingsController.matchmakingIp.text) && _gameController.password.text.isEmpty && _gameController.autoStartGameServer(),
      onChanged: (value) {
        if(!isLocalHost(_settingsController.matchmakingIp.text)){
          showMessage("This option isn't available when the matchmaker isn't set to 127.0.0.1");
          return;
        }

        if(_gameController.password.text.isNotEmpty){
          showMessage("This option isn't available when the password isn't empty(LawinV2)");
          return;
        }

        _gameController.autoStartGameServer.value = value;
      }
  );
}