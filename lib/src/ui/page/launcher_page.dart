
import 'dart:async';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/material.dart' show Icons;
import 'package:get/get.dart';
import 'package:reboot_launcher/src/ui/controller/build_controller.dart';
import 'package:reboot_launcher/src/ui/controller/game_controller.dart';
import 'package:reboot_launcher/src/ui/controller/settings_controller.dart';
import 'package:reboot_launcher/src/ui/widget/home/launch_button.dart';
import 'package:reboot_launcher/src/ui/widget/home/version_selector.dart';
import 'package:reboot_launcher/src/ui/widget/shared/setting_tile.dart';
import 'package:url_launcher/url_launcher.dart';

import '../../model/update_status.dart';
import '../../util/checks.dart';
import '../../util/reboot.dart';
import '../controller/update_controller.dart';

class LauncherPage extends StatefulWidget {
  const LauncherPage(
      {Key? key})
      : super(key: key);

  @override
  State<LauncherPage> createState() => _LauncherPageState();
}

class _LauncherPageState extends State<LauncherPage> {
  final GameController _gameController = Get.find<GameController>();
  final SettingsController _settingsController = Get.find<SettingsController>();
  final BuildController _buildController = Get.find<BuildController>();
  late final RxBool _showPasswordTrailing = RxBool(_gameController.password.text.isNotEmpty);

  @override
  void initState() {
    if(_gameController.updateStatus() == UpdateStatus.waiting) {
      _startUpdater();
      _setupBuildWarning();
    }

    super.initState();
  }

  void _setupBuildWarning() {
    void onCancelWarning() => WidgetsBinding.instance.addPostFrameCallback((_) {
      if(!mounted) {
        return;
      }

      showSnackbar(context, const Snackbar(content: Text("Download cancelled")));
      _buildController.cancelledDownload(false);
    });
    _buildController.cancelledDownload.listen((value) => value ? onCancelWarning() : {});
  }

  Future<void> _startUpdater() async {
    if(!_settingsController.autoUpdate()){
      _gameController.updateStatus.value = UpdateStatus.success;
      return;
    }

    _gameController.updateStatus.value = UpdateStatus.started;
    try {
      updateTime = await downloadRebootDll(_settingsController.updateUrl.text, updateTime);
      _gameController.updateStatus.value = UpdateStatus.success;
    }catch(_) {
      _gameController.updateStatus.value = UpdateStatus.error;
      rethrow;
    }
  }

  @override
  Widget build(BuildContext context) => Obx(() => !_settingsController.autoUpdate() || _gameController.updateStatus().isDone() ? _homePage : _updateScreen);

  Widget get _homePage => Column(
    crossAxisAlignment: CrossAxisAlignment.start,
    mainAxisSize: MainAxisSize.max,
    children: [
      AnimatedSwitcher(
        duration: const Duration(milliseconds: 300),
        child: _gameController.updateStatus() == UpdateStatus.error ? _updateError : const SizedBox(),
      ),
      AnimatedSize(
        duration: const Duration(milliseconds: 300),
        child: SizedBox(height: _gameController.updateStatus() == UpdateStatus.error ? 16.0 : 0.0),
      ),
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
            title: "Browse available servers",
            subtitle: "Discover new game servers that fit your play-style",
            content: Button(
                onPressed: () => launchUrl(Uri.parse("https://google.com/search?q=One+Day+This+Will+Be+Ready")),
                child: const Text("Browse")
            ),
            isChild: true
          )
        ]
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
      ),
      const Expanded(child: SizedBox()),
      const LaunchButton(
        host: false
      )
    ],
  );

  Widget get _updateScreen => Row(
    mainAxisAlignment: MainAxisAlignment.center,
    children: [
      Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: const [
          ProgressRing(),
          SizedBox(height: 16.0),
          Text("Updating Reboot DLL...")
        ],
      ),
    ],
  );

  Widget get _updateError => MouseRegion(
    cursor: SystemMouseCursors.click,
    child: GestureDetector(
      onTap: () => _startUpdater(),
      child: const SizedBox(
          width: double.infinity,
          child: InfoBar(
              title: Text("The reboot dll couldn't be downloaded: click here to try again"),
              severity: InfoBarSeverity.info
          )
      ),
    ),
  );
}