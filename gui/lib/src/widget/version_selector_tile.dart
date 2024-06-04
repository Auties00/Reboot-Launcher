import 'package:fluent_ui/fluent_ui.dart' hide FluentIcons;
import 'package:fluentui_system_icons/fluentui_system_icons.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/widget/setting_tile.dart';
import 'package:reboot_launcher/src/widget/version_selector.dart';

SettingTile get versionSelectSettingTile => SettingTile(
    icon: Icon(
        FluentIcons.play_24_regular
    ),
    title: Text(translations.selectFortniteName),
    subtitle: Text(translations.selectFortniteDescription),
    contentWidth: null,
    content: ConstrainedBox(
        constraints: BoxConstraints(
            minWidth: SettingTile.kDefaultContentWidth,
        ),
        child: const VersionSelector()
    )
);