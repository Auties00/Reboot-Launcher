
import 'dart:async';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_launcher/src/controller/build_controller.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/dialog/dialog.dart';
import 'package:reboot_launcher/src/widget/home/game_type_selector.dart';
import 'package:reboot_launcher/src/widget/home/launch_button.dart';
import 'package:reboot_launcher/src/widget/home/username_box.dart';
import 'package:reboot_launcher/src/widget/home/version_selector.dart';

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
  final BuildController _buildController = Get.find<BuildController>();

  @override
  void initState() {
    if(_gameController.updater == null){
      _gameController.updater = compute(downloadRebootDll, _updateTime)
        ..then((value) => _updateTime = value)
        ..then((value) => _gameController.updated = true);
      _buildController.cancelledDownload
          .listen((value) => value ? _onCancelWarning() : {});
    }

    super.initState();
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
  Widget build(BuildContext context) {
    return FutureBuilder(
        future: _gameController.updater ?? Future.value(true),
        builder: (context, snapshot) {
          if (!_gameController.updated && !snapshot.hasData && !snapshot.hasError) {
            return Row(
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
          }

          return Column(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              if(snapshot.hasError)
                _createUpdateError(snapshot),
              UsernameBox(),
              const VersionSelector(),
              GameTypeSelector(),
              const LaunchButton()
            ],
          );
        }
    );
  }

  Widget _createUpdateError(AsyncSnapshot<Object?> snapshot) {
    return MouseRegion(
      cursor: SystemMouseCursors.click,
      child: GestureDetector(
        onTap: () {
          showDialog(
              context: context,
              builder: (context) => ErrorDialog(
                  exception: snapshot.error!,
                  stackTrace: snapshot.stackTrace!,
                  errorMessageBuilder: (exception) => "Cannot update Reboot dll: ${snapshot.error}"
              )
          );
        },
        child: const SizedBox(
          width: double.infinity,
          child: InfoBar(
              title: Text("Cannot update dll"),
              severity: InfoBarSeverity.info
          ),
        )
      ),
    );
  }
}