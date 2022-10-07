import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/widget/file_selector.dart';
import 'package:reboot_launcher/src/widget/smart_switch.dart';

class SettingsPage extends StatelessWidget {
  final SettingsController _settingsController = Get.find<SettingsController>();

  SettingsPage({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return Stack(
      children: [
       Form(
         autovalidateMode: AutovalidateMode.always,
         child:  Column(
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
                 validator: _checkDll
             ),

             FileSelector(
                 label: "Console DLL",
                 placeholder: "Type the path to the console dll",
                 controller: _settingsController.consoleDll,
                 windowTitle: "Select a dll",
                 folder: false,
                 extension: "dll",
                 validator: _checkDll
             ),

             FileSelector(
                 label: "Cranium DLL",
                 placeholder: "Type the path to the cranium dll",
                 controller: _settingsController.craniumDll,
                 windowTitle: "Select a dll",
                 folder: false,
                 extension: "dll",
                 validator: _checkDll
             ),

             SmartSwitch(
                 value: _settingsController.autoUpdate,
                 label: "Update DLLs"
             ),
           ],
         )
       ),

        const Align(
            alignment: Alignment.bottomRight,
            child: Text("Version 3.11${kDebugMode ? '-DEBUG' : ''}")
        )
      ],
    );
  }

  String? _checkDll(String? text) {
    if (text == null || text.isEmpty) {
      return "Empty dll path";
    }

    if (!File(text).existsSync()) {
      return "This dll doesn't exist";
    }

    if (!text.endsWith(".dll")) {
      return "This file is not a dll";
    }

    return null;
  }
}
