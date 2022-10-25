import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/model/server_type.dart';
import 'package:reboot_launcher/src/widget/shared/smart_switch.dart';

import '../util/checks.dart';
import '../widget/shared/file_selector.dart';
import '../widget/shared/smart_input.dart';

class SettingsPage extends StatelessWidget {
  final ServerController _serverController = Get.find<ServerController>();
  final SettingsController _settingsController = Get.find<SettingsController>();

  SettingsPage({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.all(12.0),
      child: Column(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Tooltip(
              message: "The hostname of the server that hosts the multiplayer matches",
              child: Obx(() => SmartInput(
                  label: "Matchmaking Host",
                  placeholder:
                  "Type the hostname of the server that hosts the multiplayer matches",
                  controller: _settingsController.matchmakingIp,
                  validatorMode: AutovalidateMode.always,
                  validator: checkMatchmaking,
                  enabled: _serverController.type() == ServerType.embedded
              ))
            ),
            Tooltip(
              message: "The dll that is injected when a server is launched",
              child: FileSelector(
                  label: "Reboot DLL",
                  placeholder: "Type the path to the reboot dll",
                  controller: _settingsController.rebootDll,
                  windowTitle: "Select a dll",
                  folder: false,
                  extension: "dll",
                  validator: checkDll,
                  validatorMode: AutovalidateMode.always),
            ),
            Tooltip(
              message: "The dll that is injected when a client is launched",
              child: FileSelector(
                  label: "Console DLL",
                  placeholder: "Type the path to the console dll",
                  controller: _settingsController.consoleDll,
                  windowTitle: "Select a dll",
                  folder: false,
                  extension: "dll",
                  validator: checkDll,
                  validatorMode: AutovalidateMode.always),
            ),
            Tooltip(
                message: "The dll that is injected to make the game work",
                child: FileSelector(
                    label: "Cranium DLL",
                    placeholder: "Type the path to the cranium dll",
                    controller: _settingsController.craniumDll,
                    windowTitle: "Select a dll",
                    folder: false,
                    extension: "dll",
                    validator: checkDll,
                    validatorMode: AutovalidateMode.always))
          ]),
    );
  }
}
