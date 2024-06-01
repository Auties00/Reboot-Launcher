import 'package:fluent_ui/fluent_ui.dart' as fluentUi show FluentIcons;
import 'package:fluent_ui/fluent_ui.dart' hide FluentIcons;
import 'package:fluentui_system_icons/fluentui_system_icons.dart';
import 'package:flutter/services.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/matchmaker_controller.dart';
import 'package:reboot_launcher/src/dialog/implementation/data.dart';
import 'package:reboot_launcher/src/page/abstract/page.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/widget/server_start_button.dart';
import 'package:reboot_launcher/src/widget/server_type_selector.dart';
import 'package:reboot_launcher/src/widget/setting_tile.dart';
import 'package:url_launcher/url_launcher.dart';

class MatchmakerPage extends RebootPage {
  const MatchmakerPage({Key? key}) : super(key: key);

  @override
  RebootPageState<MatchmakerPage> createState() => _MatchmakerPageState();

  @override
  String get name => translations.matchmakerName;

  @override
  String get iconAsset => "assets/images/matchmaker.png";

  @override
  bool hasButton(String? pageName) => pageName == null;

  @override
  RebootPageType get type => RebootPageType.matchmaker;
}

class _MatchmakerPageState extends RebootPageState<MatchmakerPage> {
  final MatchmakerController _matchmakerController = Get.find<MatchmakerController>();

  @override
  Widget? get button => const ServerButton(
      backend: false
  );

  @override
  List<Widget> get settings => [
    _type,
    _hostName,
    _port,
    _gameServerAddress,
    _installationDirectory,
    _resetDefaults
  ];

  Widget get _gameServerAddress => Obx(() {
    if(_matchmakerController.type.value != ServerType.embedded) {
      return const SizedBox.shrink();
    }

    return SettingTile(
        icon: Icon(
            FluentIcons.stream_input_20_regular
        ),
        title: Text(translations.matchmakerConfigurationAddressName),
        subtitle: Text(translations.matchmakerConfigurationAddressDescription),
        content: TextFormBox(
            placeholder: translations.matchmakerConfigurationAddressName,
            controller: _matchmakerController.gameServerAddress,
            focusNode: _matchmakerController.gameServerAddressFocusNode
        )
    );
  });

  Widget get _port => Obx(() {
    if(_matchmakerController.type.value == ServerType.embedded) {
      return const SizedBox.shrink();
    }

    return SettingTile(
        icon: Icon(
            fluentUi.FluentIcons.number_field
        ),
        title: Text(translations.matchmakerConfigurationPortName),
        subtitle: Text(translations.matchmakerConfigurationPortDescription),
        content: TextFormBox(
            placeholder: translations.matchmakerConfigurationPortName,
            controller: _matchmakerController.port,
            keyboardType: TextInputType.number,
            inputFormatters: [
              FilteringTextInputFormatter.digitsOnly
            ]
        )
    );
  });

  Widget get _hostName => Obx(() {
    if(_matchmakerController.type.value != ServerType.remote) {
      return const SizedBox.shrink();
    }

    return SettingTile(
        icon: Icon(
            FluentIcons.globe_24_regular
        ),
        title: Text(translations.matchmakerConfigurationHostName),
        subtitle: Text(translations.matchmakerConfigurationHostDescription),
        content: TextFormBox(
            placeholder: translations.matchmakerConfigurationHostName,
            controller: _matchmakerController.host
        )
    );
  });

  Widget get _type => SettingTile(
      icon: Icon(
          FluentIcons.people_24_regular
      ),
      title: Text(translations.matchmakerTypeName),
      subtitle: Text(translations.matchmakerTypeDescription),
      content: const ServerTypeSelector(
          backend: false
      )
  );

  Widget get _installationDirectory => Obx(() {
    if(_matchmakerController.type.value != ServerType.embedded) {
      return const SizedBox.shrink();
    }

    return SettingTile(
        icon: Icon(
            FluentIcons.folder_24_regular
        ),
        title: Text(translations.matchmakerInstallationDirectoryName),
        subtitle: Text(translations.matchmakerInstallationDirectoryDescription),
        content: Button(
            onPressed: () => launchUrl(matchmakerDirectory.uri),
            child: Text(translations.matchmakerInstallationDirectoryContent)
        )
    );
  });

  SettingTile get _resetDefaults => SettingTile(
      icon: Icon(
          FluentIcons.arrow_reset_24_regular
      ),
      title: Text(translations.matchmakerResetDefaultsName),
      subtitle: Text(translations.matchmakerResetDefaultsDescription),
      content: Button(
        onPressed: () => showResetDialog(_matchmakerController.reset),
        child: Text(translations.matchmakerResetDefaultsContent),
      )
  );
}
