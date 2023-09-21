import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/page/abstract/page_setting.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/widget/common/setting_tile.dart';
import 'package:reboot_launcher/src/widget/version/version_selector.dart';

SettingTile get versionSelectorSettingTile => SettingTile(
    title: translations.addVersionName,
    subtitle: translations.addVersionDescription,
    content: const VersionSelector(),
    expandedContent: [
      SettingTile(
          title: translations.addLocalBuildName,
          subtitle: translations.addLocalBuildDescription,
          content: Button(
              onPressed: VersionSelector.openAddDialog,
              child: Text(translations.addLocalBuildContent)
          ),
          isChild: true
      ),
      SettingTile(
          title: translations.downloadBuildName,
          subtitle: translations.downloadBuildDescription,
          content: Button(
              onPressed: VersionSelector.openDownloadDialog,
              child: Text(translations.downloadBuildContent)
          ),
          isChild: true
      )
    ]
);

PageSetting get versionSelectorRebootSetting => PageSetting(
    name: translations.addVersionName,
    description: translations.addVersionDescription,
    children: [
      PageSetting(
          name: translations.addLocalBuildName,
          description: translations.addLocalBuildDescription,
          content: translations.addLocalBuildContent
      ),
      PageSetting(
          name: translations.downloadBuildName,
          description: translations.downloadBuildDescription,
          content: translations.downloadBuildContent
      )
    ]
);