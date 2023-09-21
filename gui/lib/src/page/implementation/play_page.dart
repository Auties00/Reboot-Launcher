import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/hosting_controller.dart';
import 'package:reboot_launcher/src/controller/matchmaker_controller.dart';
import 'package:reboot_launcher/src/page/abstract/page.dart';
import 'package:reboot_launcher/src/page/abstract/page_setting.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/page/pages.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/widget/common/setting_tile.dart';
import 'package:reboot_launcher/src/widget/game/start_button.dart';
import 'package:reboot_launcher/src/widget/version/version_selector_tile.dart';


class PlayPage extends RebootPage {
  const PlayPage({Key? key}) : super(key: key);

  @override
  RebootPageState<PlayPage> createState() => _PlayPageState();

  @override
  bool get hasButton => true;

  @override
  String get name => translations.playName;

  @override
  String get iconAsset => "assets/images/play.png";

  @override
  RebootPageType get type => RebootPageType.play;

  @override
  List<PageSetting> get settings => [
    versionSelectorRebootSetting,
    PageSetting(
        name: translations.playGameServerName,
        description: translations.playGameServerDescription,
        content: translations.playGameServerContentLocal,
        children: [
          PageSetting(
              name: translations.playGameServerHostName,
              description: translations.playGameServerHostDescription,
              content: translations.playGameServerHostName
          ),
          PageSetting(
              name: translations.playGameServerBrowserName,
              description: translations.playGameServerBrowserDescription,
              content: translations.playGameServerBrowserName
          ),
          PageSetting(
              name: translations.playGameServerCustomName,
              description: translations.playGameServerCustomDescription,
              content: translations.playGameServerCustomContent
          )
        ]
    )
  ];
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
    versionSelectorSettingTile,
    _gameServerSelector
  ];

  SettingTile get _gameServerSelector => SettingTile(
      title: translations.playGameServerName,
      subtitle: translations.playGameServerDescription,
      content: IgnorePointer(
        child: Button(
            style: ButtonStyle(
                backgroundColor: ButtonState.all(FluentTheme.of(context).resources.controlFillColorDefault)
            ),
            onPressed: () {},
            child: Obx(() {
              var address = _matchmakerController.gameServerAddress.text;
              var owner = _matchmakerController.gameServerOwner.value;
              return Text(
                  isLocalHost(address) ? translations.playGameServerContentLocal : owner != null ? translations.playGameServerContentBrowser(owner) : address,
                  textAlign: TextAlign.start
              );
            })
        ),
      ),
      expandedContent: [
        SettingTile(
            title: translations.playGameServerHostName,
            subtitle: translations.playGameServerHostDescription,
            content: Button(
                onPressed: () => pageIndex.value = RebootPageType.host.index,
                child: Text(translations.playGameServerHostName)
            ),
            isChild: true
        ),
        SettingTile(
            title: translations.playGameServerBrowserName,
            subtitle: translations.playGameServerBrowserDescription,
            content: Button(
                onPressed: () => pageIndex.value = RebootPageType.browser.index,
                child: Text(translations.playGameServerBrowserName)
            ),
            isChild: true
        ),
        SettingTile(
            title: translations.playGameServerCustomName,
            subtitle: translations.playGameServerCustomDescription,
            content: Button(
                onPressed: () {
                  pageIndex.value = RebootPageType.matchmaker.index;
                  WidgetsBinding.instance.addPostFrameCallback((_) => _matchmakerController.gameServerAddressFocusNode.requestFocus());
                },
                child: Text(translations.playGameServerCustomContent)
            ),
            isChild: true
        )
      ]
  );
}