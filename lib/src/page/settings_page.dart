import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/widget/shared/smart_switch.dart';

import '../util/checks.dart';
import '../widget/os/file_selector.dart';

class SettingsPage extends StatelessWidget {
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
             FileSelector(
                 label: "Reboot DLL",
                 placeholder: "Type the path to the reboot dll",
                 controller: _settingsController.rebootDll,
                 windowTitle: "Select a dll",
                 folder: false,
                 extension: "dll",
                 validator: checkDll,
                 validatorMode: AutovalidateMode.always
             ),

             FileSelector(
                 label: "Console DLL",
                 placeholder: "Type the path to the console dll",
                 controller: _settingsController.consoleDll,
                 windowTitle: "Select a dll",
                 folder: false,
                 extension: "dll",
                 validator: checkDll,
                 validatorMode: AutovalidateMode.always
             ),

             FileSelector(
                 label: "Cranium DLL",
                 placeholder: "Type the path to the cranium dll",
                 controller: _settingsController.craniumDll,
                 windowTitle: "Select a dll",
                 folder: false,
                 extension: "dll",
                 validator: checkDll,
                 validatorMode: AutovalidateMode.always
             ),

             SmartSwitch(
                 value: _settingsController.autoUpdate,
                 label: "Update DLLs"
             )
           ]
      ),
    );
  }
}
