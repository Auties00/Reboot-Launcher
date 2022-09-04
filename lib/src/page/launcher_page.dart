import 'dart:async';
import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/util/game_process_controller.dart';
import 'package:reboot_launcher/src/util/generic_controller.dart';
import 'package:reboot_launcher/src/util/version_controller.dart';
import 'package:reboot_launcher/src/widget/deployment_selector.dart';
import 'package:reboot_launcher/src/widget/launch_button.dart';
import 'package:reboot_launcher/src/widget/username_box.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../widget/version_selector.dart';

class LauncherPage extends StatelessWidget {
  final TextEditingController usernameController;
  final VersionController versionController;
  final GenericController<bool> rebootController;
  final GenericController<Process?> serverController;
  final GenericController<bool> localController;
  final GameProcessController gameProcessController;
  final GenericController<bool> startedGameController;
  final GenericController<bool> startedServerController;
  final StreamController _streamController = StreamController();

  LauncherPage(
      {Key? key,
      required this.usernameController,
      required this.versionController,
      required this.rebootController,
      required this.serverController,
      required this.localController,
      required this.gameProcessController,
      required this.startedGameController,
      required this.startedServerController})
      : super(key: key);

  @override
  Widget build(BuildContext context) {
    return Column(
      mainAxisAlignment: MainAxisAlignment.spaceBetween,
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        StreamBuilder(
            stream: _streamController.stream,
            builder: (context, snapshot) => UsernameBox(
                controller: usernameController,
                rebootController: rebootController)),
        VersionSelector(
          controller: versionController,
        ),
        DeploymentSelector(
            controller: rebootController,
            onSelected: () => _streamController.add(null),
            enabled: false
        ),
        LaunchButton(
            usernameController: usernameController,
            versionController: versionController,
            rebootController: rebootController,
            serverController: serverController,
            localController: localController,
            gameProcessController: gameProcessController,
            startedGameController: startedGameController,
            startedServerController: startedServerController)
      ],
    );
  }
}
