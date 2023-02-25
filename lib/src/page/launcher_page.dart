
import 'dart:async';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_launcher/main.dart';
import 'package:reboot_launcher/src/controller/build_controller.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/dialog/dialog.dart';
import 'package:reboot_launcher/src/model/reboot_download.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/widget/home/game_type_selector.dart';
import 'package:reboot_launcher/src/widget/home/launch_button.dart';
import 'package:reboot_launcher/src/widget/home/version_selector.dart';
import 'package:reboot_launcher/src/widget/shared/setting_tile.dart';

import '../dialog/dialog_button.dart';
import '../util/checks.dart';
import '../util/reboot.dart';

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

  @override
  void initState() {
    if(_gameController.updater == null) {
      _startUpdater();
      _setupBuildWarning();
    }

    super.initState();
  }

  void _setupBuildWarning() {
    _buildController.cancelledDownload
        .listen((value) => value ? _onCancelWarning() : {});
  }

  void _startUpdater() {
    _gameController.updater = StreamController.broadcast();
    downloadRebootDll(_settingsController.updateUrl.text, _updateTime)
      ..then((result) async {
        if(!result.hasError){
          _updateTime = result.updateTime;
          _gameController.updated = true;
          _gameController.failing = false;
          _gameController.error = false;
          _gameController.updater?.add(true);
          return;
        }

        if(_gameController.failing){
          _gameController.updated = false;
          _gameController.failing = false;
          _gameController.error = true;
          _gameController.updater?.add(false);
          return;
        }

        _gameController.failing = true;
        showDialog(
            context: appKey.currentContext!,
            builder: (context) => InfoDialog(
              text: "An error occurred while downloading the reboot dll: this usually means that your antivirus flagged it. "
                  "Do you want to add an exclusion to Windows Defender to fix the issue? "
                  "If you are using a different antivirus disable it manually as this won't work. ",
              buttons: [
                ErrorDialog.createCopyErrorButton(
                    error: result.error ?? Exception("Unknown error"),
                    stackTrace: result.stackTrace,
                    type: ButtonType.secondary,
                    onClick: () {
                      Navigator.pop(context);
                      _gameController.updated = false;
                      _gameController.failing = false;
                      _gameController.error = true;
                      _gameController.updater?.add(false);
                    }
                ),
                DialogButton(
                    text: "Add",
                    type: ButtonType.primary,
                    onTap: () async {
                      Navigator.pop(context);
                      var binary = await loadBinary("antivirus.bat", true);
                      var result = await runElevated(binary.path, "");
                      if(!result) {
                        _gameController.failing = false;
                      }

                      _startUpdater();
                    }
                ),
              ],
            )
        );
      })
      ..catchError((error, stackTrace) {
        _gameController.error = true;
        _gameController.updater?.add(false);
        return RebootDownload(0, error, stackTrace);
      });
  }

  int? get _updateTime {
    var storage = GetStorage("update");
    return storage.read("last_update_v2");
  }

  set _updateTime(int? updateTime) {
    var storage = GetStorage("update");
    storage.write("last_update_v2", updateTime);
  }

  void _onCancelWarning() {
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if(!mounted) {
        return;
      }

      showSnackbar(context,
          const Snackbar(content: Text("Download cancelled")));
      _buildController.cancelledDownload(false);
    });
  }

  @override
  Widget build(BuildContext context) => StreamBuilder<bool>(
      stream: _gameController.updater!.stream,
      builder: (context, snapshot) => !_gameController.updated && !_gameController.error ? _updateScreen : _homeScreen
  );

  Widget get _homeScreen => Column(
    crossAxisAlignment: CrossAxisAlignment.start,
    children: [
      AnimatedSwitcher(
        duration: const Duration(milliseconds: 300),
        child: _gameController.error ? _updateError : const SizedBox(),
      ),
      AnimatedSize(
        duration: const Duration(milliseconds: 300),
        child: SizedBox(height: _gameController.error ? 16.0 : 0.0),
      ),
      SettingTile(
          title: "Username",
          subtitle: "Enter the name that others will see once you are in-game",
          content: TextFormBox(
              placeholder: "username",
              controller: _gameController.username,
              validator: checkMatchmaking,
              autovalidateMode: AutovalidateMode.always
          )
      ),
      const SizedBox(
        height: 16.0,
      ),
      SettingTile(
        title: "Matchmaking host",
        subtitle: "Enter the IP address of the game server hosting the match",
        content: TextFormBox(
            placeholder: "ip:port",
            controller: _settingsController.matchmakingIp,
            validator: checkMatchmaking,
            autovalidateMode: AutovalidateMode.always
        ),
        expandedContent: [
          ListTile(
            title: const Text(
                "Automatically start a game server",
              style: TextStyle(
                  fontSize: 14
              ),
            ),
            subtitle: const Text("Choose whether an headless server should be automatically started when matchmaking is on localhost"),
            trailing: Obx(() => ToggleSwitch(
                checked: _gameController.autostartGameServer(),
                onChanged: (value) => _gameController.autostartGameServer.value = value
            ))
          ),
        ],
      ),
      const SizedBox(
        height: 16.0,
      ),
      SettingTile(
        title: "Version",
        subtitle: "Select the version of Fortnite you want to play with your friends",
        content: const VersionSelector(),
        expandedContent: [
          ListTile(
            title: const Text(
                "Add a version from this PC's local storage",
              style: TextStyle(
                fontSize: 14
              ),
            ),
            trailing: Button(
              onPressed: () => VersionSelector.openAddDialog(context),
              child: const Text("Add build "),
            ),
          ),

          ListTile(
            title: const Text(
              "Download any version from the cloud",
              style: TextStyle(
                  fontSize: 14
              ),
            ),
            trailing: Button(
              onPressed: () => VersionSelector.openDownloadDialog(context),
              child: const Text("Download"),
            ),
          ),
        ]
      ),
      const SizedBox(
        height: 16.0,
      ),
      SettingTile(
          title: "Instance type",
          subtitle: "Select the type of instance you want to launch",
          content: GameTypeSelector()
      ),
      const Expanded(child: SizedBox()),
      const LaunchButton()
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

  Widget get _updateError {
    return MouseRegion(
      cursor: SystemMouseCursors.click,
      child: GestureDetector(
        onTap: () {
          _gameController.updated = false;
          _gameController.failing = false;
          _gameController.error = false;
          _gameController.updater?.add(false);
          _startUpdater();
        },
        child: const SizedBox(
          width: double.infinity,
          child: InfoBar(
              title: Text("The Reboot dll wasn't downloaded: disable your antivirus or proxy and click here to try again"
              ),
              severity: InfoBarSeverity.info
          )
        ),
      ),
    );
  }
}