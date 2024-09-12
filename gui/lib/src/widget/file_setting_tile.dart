import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart' hide FluentIcons;
import 'package:fluentui_system_icons/fluentui_system_icons.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/widget/file_selector.dart';
import 'package:reboot_launcher/src/widget/setting_tile.dart';

SettingTile createFileSetting({required String title, required String description, required TextEditingController controller, required void Function() onReset}) {
  final obx = RxString(controller.text);
  controller.addListener(() => obx.value = controller.text);
    return SettingTile(
    icon: Icon(
        FluentIcons.document_24_regular
    ),
    title: Text(title),
    subtitle: Text(description),
    content: Row(
        children: [
            Expanded(
                child: FileSelector(
                    placeholder: translations.selectPathPlaceholder,
                    windowTitle: translations.selectPathWindowTitle,
                    controller: controller,
                    validator: _checkDll,
                    extension: "dll",
                    folder: false,
                    validatorMode: AutovalidateMode.always
                ),
            ),
            const SizedBox(width: 8.0),
            Obx(() => Padding(
              padding: EdgeInsets.only(
                  bottom: _checkDll(obx.value) == null ? 0.0 : 20.0
              ),
              child: Button(
                  style: ButtonStyle(
                      padding: ButtonState.all(EdgeInsets.zero)
                  ),
                  onPressed: onReset,
                  child: SizedBox.square(
                      dimension: 30,
                      child: Icon(
                          FluentIcons.arrow_reset_24_regular
                      ),
                  )
              ),
            ))
        ],
    )
);
}

String? _checkDll(String? text) {
    if (text == null || text.isEmpty) {
        return translations.invalidDllPath;
    }

    final file = File(text);
    if (!file.existsSync()) {
        return translations.dllDoesNotExist;
    }

    if (!text.endsWith(".dll")) {
        return translations.invalidDllExtension;
    }

    return null;
}