import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/services.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/matchmaker_controller.dart';
import 'package:reboot_launcher/src/dialog/implementation/data.dart';
import 'package:reboot_launcher/src/page/abstract/page.dart';
import 'package:reboot_launcher/src/page/abstract/page_setting.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/widget/common/setting_tile.dart';
import 'package:reboot_launcher/src/widget/server/start_button.dart';
import 'package:reboot_launcher/src/widget/server/type_selector.dart';
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
  bool get hasButton => true;

  @override
  RebootPageType get type => RebootPageType.matchmaker;

  @override
  List<PageSetting> get settings => [
    PageSetting(
        name: translations.matchmakerConfigurationName,
        description: translations.matchmakerConfigurationDescription,
        children: [
          PageSetting(
              name: translations.matchmakerConfigurationHostName,
              description: translations.matchmakerConfigurationHostDescription
          ),
          PageSetting(
              name: translations.matchmakerConfigurationPortName,
              description: translations.matchmakerConfigurationPortDescription
          ),
          PageSetting(
              name: translations.matchmakerConfigurationDetachedName,
              description: translations.matchmakerConfigurationDetachedDescription
          )
        ]
    ),
    PageSetting(
        name: translations.matchmakerInstallationDirectoryName,
        description: translations.matchmakerInstallationDirectoryDescription,
        content: translations.matchmakerInstallationDirectoryContent
    ),
    PageSetting(
        name: translations.matchmakerResetDefaultsName,
        description: translations.matchmakerResetDefaultsDescription,
        content: translations.matchmakerResetDefaultsContent
    )
  ];
}

class _MatchmakerPageState extends RebootPageState<MatchmakerPage> {
  final MatchmakerController _matchmakerController = Get.find<MatchmakerController>();

  @override
  Widget? get button => const ServerButton(
      authenticator: false
  );

  @override
  List<Widget> get settings => [
    _configuration,
    _installationDirectory,
    _resetDefaults
  ];

  Widget get _configuration => Obx(() => SettingTile(
      title: translations.matchmakerConfigurationName,
      subtitle: translations.matchmakerConfigurationDescription,
      content: const ServerTypeSelector(
          authenticator: false
      ),
      expandedContent: [
        if(_matchmakerController.type.value == ServerType.remote)
          SettingTile(
              title: translations.matchmakerConfigurationHostName,
              subtitle: translations.matchmakerConfigurationHostDescription,
              isChild: true,
              content: TextFormBox(
                  placeholder: translations.matchmakerConfigurationHostName,
                  controller: _matchmakerController.host
              )
          ),
        if(_matchmakerController.type.value != ServerType.embedded)
          SettingTile(
              title: translations.matchmakerConfigurationPortName,
              subtitle: translations.matchmakerConfigurationPortDescription,
              isChild: true,
              content: TextFormBox(
                  placeholder: translations.matchmakerConfigurationPortName,
                  controller: _matchmakerController.port,
                  keyboardType: TextInputType.number,
                  inputFormatters: [
                    FilteringTextInputFormatter.digitsOnly
                  ]
              )
          ),
        if(_matchmakerController.type.value == ServerType.embedded)
          SettingTile(
              title: translations.matchmakerConfigurationAddressName,
              subtitle: translations.matchmakerConfigurationAddressDescription,
              isChild: true,
              content: TextFormBox(
                  placeholder: translations.matchmakerConfigurationAddressName,
                  controller: _matchmakerController.gameServerAddress,
                  focusNode: _matchmakerController.gameServerAddressFocusNode
              )
          ),
        if(_matchmakerController.type.value == ServerType.embedded)
          SettingTile(
            title: translations.matchmakerConfigurationDetachedName,
            subtitle: translations.matchmakerConfigurationDetachedDescription,
            contentWidth: null,
            isChild: true,
            content: Obx(() => Row(
              children: [
                Text(
                    _matchmakerController.detached.value ? translations.on : translations.off
                ),
                const SizedBox(
                    width: 16.0
                ),
                ToggleSwitch(
                    checked: _matchmakerController.detached.value,
                    onChanged: (value) => _matchmakerController.detached.value = value
                ),
              ],
            )),
          )
      ]
  ));

  SettingTile get _installationDirectory => SettingTile(
      title: translations.matchmakerInstallationDirectoryName,
      subtitle: translations.matchmakerInstallationDirectoryDescription,
      content: Button(
          onPressed: () => launchUrl(matchmakerDirectory.uri),
          child: Text(translations.matchmakerInstallationDirectoryContent)
      )
  );

  SettingTile get _resetDefaults => SettingTile(
      title: translations.matchmakerResetDefaultsName,
      subtitle: translations.matchmakerResetDefaultsDescription,
      content: Button(
        onPressed: () => showResetDialog(_matchmakerController.reset),
        child: Text(translations.matchmakerResetDefaultsContent),
      )
  );
}
