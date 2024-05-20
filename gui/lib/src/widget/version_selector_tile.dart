import 'package:fluent_ui/fluent_ui.dart' hide FluentIcons;
import 'package:fluentui_system_icons/fluentui_system_icons.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/widget/setting_tile.dart';
import 'package:reboot_launcher/src/widget/version_selector.dart';

SettingTile get versionSelectSettingTile => SettingTile(
  icon: Icon(
      FluentIcons.play_24_regular
  ),
  title: Text(translations.manageVersionsName),
  subtitle: Text(translations.manageVersionsDescription),
  content: const VersionSelector(),
  children: [
    _selectVersionTile,
    _addLocalTile,
    _downloadTile
  ],
);

Widget get _selectVersionTile => Obx(() {
  final gameController = Get.find<GameController>();
  if(gameController.hasNoVersions) {
    return const SizedBox();
  }

  return SettingTile(
      icon: Icon(
          FluentIcons.play_24_regular
      ),
      title: Text(translations.selectFortniteName),
      subtitle: Text(translations.selectFortniteDescription),
      content: const VersionSelector()
  );
});

SettingTile get _downloadTile => SettingTile(
    icon: Icon(
        FluentIcons.arrow_download_24_regular
    ),
    title: Text(translations.downloadBuildName),
    subtitle: Text(translations.downloadBuildDescription),
    content: Button(
        onPressed: VersionSelector.openDownloadDialog,
        child: Text(translations.downloadBuildContent)
    )
);

SettingTile get _addLocalTile => SettingTile(
    icon: Icon(
        FluentIcons.folder_add_24_regular
    ),
    title: Text(translations.addLocalBuildName),
    subtitle: Text(translations.addLocalBuildDescription),
    content: Button(
        onPressed: VersionSelector.openAddDialog,
        child: Text(translations.addLocalBuildContent)
    )
);