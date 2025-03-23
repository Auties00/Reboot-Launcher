import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart' as fluentIcons show FluentIcons;
import 'package:fluent_ui/fluent_ui.dart' hide FluentIcons;
import 'package:fluentui_system_icons/fluentui_system_icons.dart';
import 'package:flutter/foundation.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/widget/file/file_selector.dart';
import 'package:reboot_launcher/src/widget/fluent/setting_tile.dart';

const double _kButtonDimensions = 30;
const double _kButtonSpacing = 8;

SettingTile createFileSetting({
  required GlobalKey<TextFormBoxState> key,
  required String title,
  required String description,
  required TextEditingController controller,
  required void Function() onReset
}) {
  final obx = RxnString();
  final selecting = RxBool(false);
  return SettingTile(
      icon: Icon(
          FluentIcons.document_24_regular
      ),
      title: Text(title),
      subtitle: Text(description),
      contentWidth: SettingTile.kDefaultContentWidth + _kButtonDimensions,
      content: Row(
        children: [
          Expanded(
            child: FileSelector(
              placeholder: translations.selectPathPlaceholder,
              windowTitle: translations.selectPathWindowTitle,
              controller: controller,
              validator: (text) {
                final result = _checkDll(text);
                print("Called validator: $result");
                obx.value = result;
                return result;
              },
              extension: "dll",
              folder: false,
              validatorMode: AutovalidateMode.always,
              allowNavigator: false,
              validatorKey: key
            ),
          ),
          const SizedBox(width: _kButtonSpacing),
          Obx(() => Padding(
            padding: EdgeInsets.only(
                bottom: obx.value == null ? 0.0 : 20.0
            ),
            child: Tooltip(
              message: translations.selectFile,
              child: Button(
                  style: ButtonStyle(
                      padding: WidgetStateProperty.all(EdgeInsets.zero)
                  ),
                  onPressed: () => _onPressed(selecting, controller),
                  child: SizedBox.square(
                    dimension: _kButtonDimensions,
                    child: Icon(
                        fluentIcons.FluentIcons.open_folder_horizontal
                    ),
                  )
              ),
            ),
          )),
          const SizedBox(width: _kButtonSpacing),
          Obx(() => Padding(
            padding: EdgeInsets.only(
                bottom: obx.value == null ? 0.0 : 20.0
            ),
            child: Tooltip(
              message: translations.reset,
              child: Button(
                  style: ButtonStyle(
                      padding: WidgetStateProperty.all(EdgeInsets.zero)
                  ),
                  onPressed: onReset,
                  child: SizedBox.square(
                    dimension: _kButtonDimensions,
                    child: Icon(
                        FluentIcons.arrow_reset_24_regular
                    ),
                  )
              ),
            ),
          ))
        ],
      )
  );
}

void _onPressed(RxBool selecting, TextEditingController controller) {
  if(selecting.value){
    return;
  }

  selecting.value = true;
  compute(openFilePicker, "dll")
      .then((value) => _updateText(controller, value))
      .then((_) => selecting.value = false);
}

void _updateText(TextEditingController controller, String? value) {
  final text = value ?? controller.text;
  controller.text = text;
  controller.selection = TextSelection.collapsed(offset: text.length);
}

String? _checkDll(String? text) {
  if (text == null || text.isEmpty) {
    return translations.invalidDllPath;
  }

  final file = File(text);
  try {
    file.readAsBytesSync();
  }catch(_) {
    return translations.dllDoesNotExist;
  }

  if (!text.endsWith(".dll")) {
    return translations.invalidDllExtension;
  }

  return null;
}