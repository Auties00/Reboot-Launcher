import 'package:fluent_ui/fluent_ui.dart' hide FluentIcons;
import 'package:fluentui_system_icons/fluentui_system_icons.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/messenger/abstract/overlay.dart';
import 'package:reboot_launcher/src/messenger/implementation/data.dart';
import 'package:reboot_launcher/src/page/abstract/page.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/util/translations.dart';
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
  final GameController _gameController = Get.find<GameController>();

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
    _resetDefaults
  ];

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
        }),
        child: Text(translations.gameResetDefaultsContent),
      )
  );
}