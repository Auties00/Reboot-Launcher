import 'dart:async';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/ui/dialog/snackbar.dart';
import 'package:reboot_launcher/src/ui/widget/home/launch_button.dart';
import 'package:reboot_launcher/src/ui/widget/home/setting_tile.dart';
import 'package:reboot_launcher/src/util/checks.dart';
import 'package:reboot_launcher/src/util/server.dart';

import '../../util/os.dart';
import '../controller/game_controller.dart';
import '../controller/settings_controller.dart';
import '../dialog/dialog.dart';
import '../dialog/dialog_button.dart';
import '../widget/home/version_selector.dart';

class InfoPage extends StatefulWidget {
  final GlobalKey<NavigatorState> navigatorKey;
  final RxInt nestedNavigation;
  const InfoPage(this.navigatorKey, this.nestedNavigation, {Key? key}) : super(key: key);

  @override
  State<InfoPage> createState() => _InfoPageState();
}

class _InfoPageState extends State<InfoPage> with AutomaticKeepAliveClientMixin {
  final SettingsController _settingsController = Get.find<SettingsController>();
  late final ScrollController _controller;

  @override
  bool get wantKeepAlive => true;

  @override
  void initState() {
    _controller = ScrollController(initialScrollOffset: _settingsController.scrollingDistance);
    _controller.addListener(() {
      _settingsController.scrollingDistance = _controller.offset;
    });
    super.initState();
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    super.build(context);
    return Navigator(
      key: widget.navigatorKey,
      initialRoute: "introduction",
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
      case "introduction":
        return _IntroductionPage(widget.navigatorKey, widget.nestedNavigation);
      case "play":
        return _PlayPage(widget.navigatorKey, widget.nestedNavigation);
      default:
        throw Exception("Unknown page: $name");
    }
  }
}

class _IntroductionPage extends StatefulWidget {
  final GlobalKey<NavigatorState> navigatorKey;
  final RxInt nestedNavigation;
  const _IntroductionPage(this.navigatorKey, this.nestedNavigation, {Key? key}) : super(key: key);

  @override
  State<_IntroductionPage> createState() => _IntroductionPageState();
}

class _IntroductionPageState extends State<_IntroductionPage> {
  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        Expanded(
          child: ListView(
            children:  [
              SettingTile(
                title: 'What is Project Reboot?',
                subtitle: 'Project Reboot allows anyone to easily host a game server for most of Fortnite\'s seasons. The project was started on Discord by Milxnor and it\'s still being actively developed. Also, it\'s open source on Github!',
                titleStyle: FluentTheme.of(context).typography.title,
                contentWidth: null,
              ),
              const SizedBox(
                height: 8.0,
              ),
              SettingTile(
                title: 'What is a game server?',
                subtitle: 'When you join a Fortnite Game, your client connects to a game server that allows you to play with others. You can join someone else\'s game server, or host one on your PC. You can host your own game server by going to the "Host" tab. Just like in Minecraft, a client needs to connect to a server hosted on a certain IP or domain. In short, remember that you need both a client and a server to play!',
                titleStyle: FluentTheme.of(context).typography.title,
                contentWidth: null,
              ),
              const SizedBox(
                height: 8.0,
              ),
              SettingTile(
                title: 'What is a client?',
                subtitle: 'A client is the actual Fortnite game. You can download any version of Fortnite from the launcher in the "Play" tab. You can also import versions from your local PC, but remember that these may be corrupted. If a local version doesn\'t work, try installing it from the launcher before reporting a bug.',
                titleStyle: FluentTheme.of(context).typography.title,
                contentWidth: null,
              ),
              const SizedBox(
                height: 8.0,
              ),
              SettingTile(
                title: 'What is a backend?',
                subtitle: 'A backend is the program that handles authentication, parties and voice chats. By default, a LawinV1 server will be started for you to play. You can use also use a backend running locally, that is on your PC, or remotely, that is on another PC. Changing the backend settings can break the client and game server: unless you are an advanced user, do not edit, for any reason, these settings! If you need to restore these settings, click on "Restore Defaults" in the "Backend" tab.',
                titleStyle: FluentTheme.of(context).typography.title,
                contentWidth: null,
              ),
              const SizedBox(
                height: 8.0,
              ),
              SettingTile(
                title: 'Do I need to update DLLs?',
                subtitle: 'No, all the files that the launcher uses are automatically updated. Though, you need to update the launcher yourself if you haven\'t downloaded it from the Microsoft Store. You can use your own DLLs by going to the "Settings" tab, but make sure that they don\'t create a console that reads IO or the launcher will stop working correctly. Unless you are an advanced user, changing these options is not recommended',
                titleStyle: FluentTheme.of(context).typography.title,
                contentWidth: null,
              ),
              const SizedBox(
                height: 8.0,
              ),
              SettingTile(
                title: 'Where can I report bugs or ask for new features?',
                subtitle: 'Go to the "Settings" tab and click on report bug. Please make sure to be as specific as possible when filing a report as it\'s crucial to make it as easy to fix/implement',
                titleStyle: FluentTheme.of(context).typography.title,
                contentWidth: null,
              )
            ],
          ),
        ),
        const SizedBox(
          height: 8.0,
        ),
        SizedBox(
          width: double.infinity,
          height: 48,
          child: Button(
              child: const Align(
                  alignment: Alignment.center,
                  child: Text("How do I play?")
              ),
              onPressed: () {
                widget.navigatorKey.currentState?.pushNamed("play");
                widget.nestedNavigation.value += 1;
              }
          ),
        )
      ],
    );
  }
}

class _PlayPage extends StatefulWidget {
  final GlobalKey<NavigatorState> navigatorKey;
  final RxInt nestedNavigation;
  const _PlayPage(this.navigatorKey, this.nestedNavigation, {Key? key}) : super(key: key);

  @override
  State<_PlayPage> createState() => _PlayPageState();
}

class _PlayPageState extends State<_PlayPage> {
  final GameController _gameController = Get.find<GameController>();
  final SettingsController _settingsController = Get.find<SettingsController>();
  final RxBool _localGameServer = RxBool(true);
  final TextEditingController _remoteGameServerController = TextEditingController();
  final StreamController _remoteGameServerStream = StreamController();

  @override
  void initState() {
    var ip = _settingsController.matchmakingIp.text;
    _remoteGameServerController.text = isLocalHost(ip) ? "" : ip;
    _remoteGameServerController.addListener(() => _remoteGameServerStream.add(null));
    super.initState();
  }

  @override
  Widget build(BuildContext context) {
    return Column(
        children: [
          Expanded(
            child: ListView(
              children: [
                SettingTile(
                  title: '1. Select a username',
                  subtitle: 'Choose a name for other players to see while you are in-game',
                  titleStyle: FluentTheme.of(context).typography.title,
                  expandedContentHeaderHeight: 80,
                  contentWidth: 0,
                  expandedContent:  [
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
                  ],
                ),
                const SizedBox(
                  height: 16.0,
                ),
                SettingTile(
                  title: '2. Download Fortnite',
                  subtitle: 'Download or import the version of Fortnite you want to play. Make sure that it\'s the same as the game server\'s you want to join!',
                  titleStyle: FluentTheme.of(context).typography.title,
                  expandedContentHeaderHeight: 80,
                  contentWidth: 0,
                  expandedContent:  [
                    const SettingTile(
                      title: "Version",
                      subtitle: "Select the version of Fortnite you want to play",
                      content: VersionSelector(),
                      isChild: true,
                    ),
                    SettingTile(
                        title: "Add a version from this PC's local storage",
                      subtitle: "Versions coming from your local disk are not guaranteed to work",
                      isChild: true,
                        content: Button(
                          onPressed: () => VersionSelector.openAddDialog(context),
                          child: const Text("Add build"),
                        ),
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
                  ],
                ),
                const SizedBox(
                  height: 16.0,
                ),
                StreamBuilder(
                  stream: _remoteGameServerStream.stream,
                  builder: (context, snapshot) => SettingTile(
                    title: '3. Choose a game server',
                    subtitle: 'Select the game server you want to use to play Fortnite.',
                    titleStyle: FluentTheme.of(context).typography.title,
                    expandedContentHeaderHeight: 80,
                    contentWidth: 0,
                    expandedContent:  [
                      SettingTile(
                          title: "Local Game Server",
                          subtitle: "Select this option if you want to host the game server on your PC",
                          contentWidth: null,
                          isChild: true,
                          content: Obx(() => Checkbox(
                              checked: _remoteGameServerController.text.isEmpty && _localGameServer(),
                              onChanged: (value) {
                                _localGameServer.value = value ?? false;
                                _remoteGameServerController.text = _settingsController.matchmakingIp.text = "";
                              }
                          ))
                      ),
                      SettingTile(
                          title: "Remote Game Server",
                          subtitle: "Select this option if you want to join a match hosted on someone else's pc",
                          isChild: true,
                          content: TextFormBox(
                              controller: _remoteGameServerController,
                              onChanged: (value) =>_localGameServer.value = false,
                              placeholder: "Type the game server's ip",
                              validator: checkMatchmaking
                          )
                      )
                    ],
                  ),
                )
              ],
            ),
          ),
          const SizedBox(
            height: 8.0,
          ),
          LaunchButton(
              startLabel: 'Start playing',
              stopLabel: 'Close game',
              host: false,
              check: () {
                if(_gameController.selectedVersion == null){
                  showMessage("Select a Fortnite version before continuing");
                  return false;
                }

                if(!_localGameServer() && _remoteGameServerController.text.isEmpty){
                  showMessage("Select a game server before continuing");
                  return false;
                }

                if(_localGameServer()){
                  _settingsController.matchmakingIp.text = "127.0.0.1";
                  _gameController.autoStartGameServer.value = true;
                }else {
                  _settingsController.matchmakingIp.text = _remoteGameServerController.text;
                }

                _settingsController.firstRun.value = false;
                return true;
              }
          )
        ]
    );
  }
}