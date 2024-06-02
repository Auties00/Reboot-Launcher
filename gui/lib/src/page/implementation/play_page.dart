import 'package:fluent_ui/fluent_ui.dart' hide FluentIcons;
import 'package:fluentui_system_icons/fluentui_system_icons.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/backend_controller.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/page/abstract/page.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/page/pages.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/widget/file_setting_tile.dart';
import 'package:reboot_launcher/src/widget/game_start_button.dart';
import 'package:reboot_launcher/src/widget/setting_tile.dart';
import 'package:reboot_launcher/src/widget/version_selector_tile.dart';


class PlayPage extends RebootPage {
  const PlayPage({Key? key}) : super(key: key);

  @override
  RebootPageState<PlayPage> createState() => _PlayPageState();

  @override
  bool hasButton(String? pageName) => pageName == null;

  @override
  String get name => translations.playName;

  @override
  String get iconAsset => "assets/images/play.png";

  @override
  RebootPageType get type => RebootPageType.play;
}

class _PlayPageState extends RebootPageState<PlayPage> {
  final SettingsController _settingsController = Get.find<SettingsController>();
  final GameController _gameController = Get.find<GameController>();
  final BackendController _backendController = Get.find<BackendController>();
  
  @override
  Widget? get button => LaunchButton(
      startLabel: translations.launchFortnite,
      stopLabel: translations.closeFortnite,
      host: false
  );

  @override
  List<SettingTile> get settings => [
    versionSelectSettingTile,
    _options,
    _internalFiles,
    _multiplayer
  ];

  SettingTile get _multiplayer => SettingTile(
    icon: Icon(
        FluentIcons.people_24_regular
    ),
    title: Text(translations.playGameServerName),
    subtitle: Text(translations.playGameServerDescription),
    children: [
      _hostSettingTile,
      _browseServerTile,
      _matchmakerTile,
    ],
  );

  SettingTile get _internalFiles => SettingTile(
    icon: Icon(
        FluentIcons.archive_settings_24_regular
    ),
    title: Text(translations.settingsClientName),
    subtitle: Text(translations.settingsClientDescription),
    children: [
      createFileSetting(
          title: translations.settingsClientConsoleName,
          description: translations.settingsClientConsoleDescription,
          controller: _settingsController.unrealEngineConsoleDll
      ),
      createFileSetting(
          title: translations.settingsClientAuthName,
          description: translations.settingsClientAuthDescription,
          controller: _settingsController.backendDll
      ),
      createFileSetting(
          title: translations.settingsClientMemoryName,
          description: translations.settingsClientMemoryDescription,
          controller: _settingsController.memoryLeakDll
      ),
    ],
  );

  SettingTile get _options => SettingTile(
      icon: Icon(
          FluentIcons.options_24_regular
      ),
      title: Text(translations.settingsServerOptionsName),
      subtitle: Text(translations.settingsServerOptionsSubtitle),
      children: [
        SettingTile(
            icon: Icon(
                FluentIcons.options_24_regular
            ),
            title: Text(translations.settingsClientArgsName),
            subtitle: Text(translations.settingsClientArgsDescription),
            content: TextFormBox(
              placeholder: translations.settingsClientArgsPlaceholder,
              controller: _gameController.customLaunchArgs,
            )
        )
      ]
  );

  SettingTile get _matchmakerTile => SettingTile(
    onPressed: () {
      pageIndex.value = RebootPageType.backend.index;
      WidgetsBinding.instance.addPostFrameCallback((_) => _backendController.gameServerAddressFocusNode.requestFocus());
    },
    icon: Icon(
        FluentIcons.globe_24_regular
    ),
    title: Text(translations.playGameServerCustomName),
    subtitle: Text(translations.playGameServerCustomDescription),
  );

  SettingTile get _browseServerTile => SettingTile(
    onPressed: () => pageIndex.value = RebootPageType.browser.index,
    icon: Icon(
        FluentIcons.search_24_regular
    ),
    title: Text(translations.playGameServerBrowserName),
    subtitle: Text(translations.playGameServerBrowserDescription)
  );

  SettingTile get _hostSettingTile => SettingTile(
    onPressed: () => pageIndex.value = RebootPageType.host.index,
    icon: Icon(
        FluentIcons.desktop_24_regular
    ),
    title: Text(translations.playGameServerHostName),
    subtitle: Text(translations.playGameServerHostDescription),
  );
}