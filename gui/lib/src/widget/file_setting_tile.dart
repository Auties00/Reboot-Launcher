import 'package:fluent_ui/fluent_ui.dart' hide FluentIcons;
import 'package:fluentui_system_icons/fluentui_system_icons.dart';
import 'package:reboot_launcher/src/util/checks.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/widget/file_selector.dart';
import 'package:reboot_launcher/src/widget/setting_tile.dart';

SettingTile createFileSetting({required String title, required String description, required TextEditingController controller}) => SettingTile(
    icon: Icon(
        FluentIcons.document_24_regular
    ),
    title: Text(title),
    subtitle: Text(description),
    content: FileSelector(
        placeholder: translations.selectPathPlaceholder,
        windowTitle: translations.selectPathWindowTitle,
        controller: controller,
        validator: checkDll,
        extension: "dll",
        folder: false
    )
);