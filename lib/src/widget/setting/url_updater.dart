import 'dart:async';

import 'package:file_picker/file_picker.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/dialog/snackbar.dart';

import 'package:reboot_launcher/src/util/selector.dart';

class RebootUpdaterInput extends StatefulWidget {
  const RebootUpdaterInput({Key? key}) : super(key: key);

  @override
  State<RebootUpdaterInput> createState() => _RebootUpdaterInputState();
}

class _RebootUpdaterInputState extends State<RebootUpdaterInput> {
  final SettingsController _settingsController = Get.find<SettingsController>();
  final RxBool _valid = RxBool(true);
  late String? Function(String?) validator;

  @override
  void initState() {
    validator = (value) {
      var result = value != null && Uri.tryParse(value) != null;
      WidgetsBinding.instance.addPostFrameCallback((_) => _valid.value = result);
      return result ? null : "Invalid URL";
    };

    super.initState();
  }

  @override
  Widget build(BuildContext context) {
    return InfoLabel(
        label: "Reboot Updater",
        child: Row(
          crossAxisAlignment: CrossAxisAlignment.center,
          children: [
            Obx(() => Expanded(
                child: TextFormBox(
                    controller: _settingsController.updateUrl,
                    placeholder: "Type the URL of the reboot updater",
                    validator: validator,
                    autovalidateMode: AutovalidateMode.always,
                    enabled: _settingsController.autoUpdate.value
                )
            )),
            const SizedBox(width: 16.0),
              Tooltip(
                  message: _settingsController.autoUpdate.value ? "Disable automatic updates" : "Enable automatic updates",
                  child: Obx(() => Button(
                      onPressed: () => _settingsController.autoUpdate.value = !_settingsController.autoUpdate.value,
                      child: Icon(_settingsController.autoUpdate.value ? FluentIcons.disable_updates : FluentIcons.refresh)
                  )
                  )
              )
          ],
        )
    );
  }
}