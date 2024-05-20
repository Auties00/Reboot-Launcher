import 'package:fluent_ui/fluent_ui.dart' hide FluentIcons;
import 'package:fluentui_system_icons/fluentui_system_icons.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/hosting_controller.dart';
import 'package:reboot_launcher/src/controller/matchmaker_controller.dart';
import 'package:reboot_launcher/src/page/abstract/page.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/page/pages.dart';
import 'package:reboot_launcher/src/util/translations.dart';
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
  final MatchmakerController _matchmakerController = Get.find<MatchmakerController>();
  final HostingController _hostingController = Get.find<HostingController>();
  late final RxBool _selfServer;

  @override
  void initState() {
    _selfServer = RxBool(_isLocalPlay);
    _matchmakerController.gameServerAddress.addListener(() => _selfServer.value = _isLocalPlay);
    _hostingController.started.listen((_) => _selfServer.value = _isLocalPlay);
    super.initState();
  }

  bool get _isLocalPlay => isLocalHost(_matchmakerController.gameServerAddress.text)
      && !_hostingController.started.value;

  @override
  Widget? get button => LaunchButton(
      startLabel: translations.launchFortnite,
      stopLabel: translations.closeFortnite,
      host: false
  );

  @override
  List<SettingTile> get settings => [
    versionSelectSettingTile,
    _hostSettingTile,
    _browseServerTile,
    _matchmakerTile
  ];

  SettingTile get _matchmakerTile => SettingTile(
    onPressed: () {
      pageIndex.value = RebootPageType.matchmaker.index;
      WidgetsBinding.instance.addPostFrameCallback((_) => _matchmakerController.gameServerAddressFocusNode.requestFocus());
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