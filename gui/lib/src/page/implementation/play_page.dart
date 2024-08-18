import 'package:fluent_ui/fluent_ui.dart' hide FluentIcons;
import 'package:fluentui_system_icons/fluentui_system_icons.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/dll_controller.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/messenger/abstract/overlay.dart';
import 'package:reboot_launcher/src/messenger/implementation/data.dart';
import 'package:reboot_launcher/src/messenger/implementation/onboard.dart';
import 'package:reboot_launcher/src/page/abstract/page.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/widget/file_setting_tile.dart';
import 'package:reboot_launcher/src/widget/game_start_button.dart';
import 'package:reboot_launcher/src/widget/setting_tile.dart';
import 'package:reboot_launcher/src/widget/version_selector_tile.dart';

final GlobalKey<OverlayTargetState> gameVersionOverlayTargetKey = GlobalKey();

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
  final DllController _dllController = Get.find<DllController>();
  
  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        _buildFirstLaunchInfo(),
        Expanded(
          child: super.build(context),
        )
      ],
    );
  }

  Widget _buildFirstLaunchInfo() => Obx(() {
    if(!_settingsController.firstRun.value) {
      return const SizedBox.shrink();
    }

    return Padding(
      padding: const EdgeInsets.only(
          bottom: 8.0
      ),
      child: SizedBox(
        width: double.infinity,
        child: InfoBar(
          title: Text(translations.welcomeTitle),
          severity: InfoBarSeverity.warning,
          isLong: true,
          content: SizedBox(
            width: double.infinity,
            child: Text(translations.welcomeDescription)
          ),
          action: Button(
            child: Text(translations.welcomeAction),
            onPressed: () => startOnboarding(),
          ),
          onClose: () => _settingsController.firstRun.value = false
        ),
      ),
    );
  });

  @override
  Widget? get button => LaunchButton(
      startLabel: translations.launchFortnite,
      stopLabel: translations.closeFortnite,
      host: false
  );

  @override
  List<SettingTile> get settings => [
    buildVersionSelector(
      key: gameVersionOverlayTargetKey
    ),
    _options,
    _internalFiles,
    _resetDefaults
  ];

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
          controller: _dllController.unrealEngineConsoleDll
      ),
      createFileSetting(
          title: translations.settingsClientAuthName,
          description: translations.settingsClientAuthDescription,
          controller: _dllController.backendDll
      ),
      createFileSetting(
          title: translations.settingsClientMemoryName,
          description: translations.settingsClientMemoryDescription,
          controller: _dllController.memoryLeakDll
      ),
    ],
  );

  SettingTile get _options => SettingTile(
      icon: Icon(
          FluentIcons.options_24_regular
      ),
      title: Text(translations.settingsClientOptionsName),
      subtitle: Text(translations.settingsClientOptionsDescription),
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

  SettingTile get _resetDefaults => SettingTile(
      icon: Icon(
          FluentIcons.arrow_reset_24_regular
      ),
      title: Text(translations.gameResetDefaultsName),
      subtitle: Text(translations.gameResetDefaultsDescription),
      content: Button(
        onPressed: () => showResetDialog(() {
          _gameController.reset();
          _dllController.resetGame();
        }),
        child: Text(translations.gameResetDefaultsContent),
      )
  );
}