import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/services.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/authenticator_controller.dart';
import 'package:reboot_launcher/src/page/abstract/page.dart';
import 'package:reboot_launcher/src/page/abstract/page_setting.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/widget/common/setting_tile.dart';
import 'package:reboot_launcher/src/widget/server/start_button.dart';
import 'package:reboot_launcher/src/widget/server/type_selector.dart';
import 'package:url_launcher/url_launcher.dart';

import '../../dialog/implementation/data.dart';

class AuthenticatorPage extends RebootPage {
  const AuthenticatorPage({Key? key}) : super(key: key);

  @override
  String get name => translations.authenticatorName;

  @override
  String get iconAsset => "assets/images/authenticator.png";

  @override
  RebootPageType get type => RebootPageType.authenticator;

  @override
  bool get hasButton => true;

  @override
  List<PageSetting> get settings => [
    PageSetting(
        name: translations.authenticatorConfigurationName,
        description: translations.authenticatorConfigurationDescription,
        children: [
          PageSetting(
              name: translations.authenticatorConfigurationHostName,
              description: translations.authenticatorConfigurationHostDescription
          ),
          PageSetting(
              name: translations.authenticatorConfigurationPortName,
              description: translations.authenticatorConfigurationPortDescription
          ),
          PageSetting(
              name: translations.authenticatorConfigurationDetachedName,
              description: translations.authenticatorConfigurationDetachedDescription
          )
        ]
    ),
    PageSetting(
        name: translations.authenticatorInstallationDirectoryName,
        description: translations.authenticatorInstallationDirectoryDescription,
        content: translations.authenticatorInstallationDirectoryContent
    ),
    PageSetting(
        name: translations.authenticatorResetDefaultsName,
        description: translations.authenticatorResetDefaultsDescription,
        content: translations.authenticatorResetDefaultsContent
    )
  ];

  @override
  RebootPageState<AuthenticatorPage> createState() => _AuthenticatorPageState();
}

class _AuthenticatorPageState extends RebootPageState<AuthenticatorPage> {
  final AuthenticatorController _authenticatorController = Get.find<AuthenticatorController>();

  @override
  List<Widget> get settings => [
    _configuration,
    _installationDirectory,
    _resetDefaults
  ];

  @override
  Widget get button => const ServerButton(
      authenticator: true
  );

  SettingTile get _resetDefaults => SettingTile(
      title: translations.authenticatorResetDefaultsName,
      subtitle: translations.authenticatorResetDefaultsDescription,
      content: Button(
        onPressed: () => showResetDialog(_authenticatorController.reset),
        child: Text(translations.authenticatorResetDefaultsContent),
      )
  );

  SettingTile get _installationDirectory => SettingTile(
      title: translations.authenticatorInstallationDirectoryName,
      subtitle: translations.authenticatorInstallationDirectoryDescription,
      content: Button(
          onPressed: () => launchUrl(authenticatorDirectory.uri),
          child: Text(translations.authenticatorInstallationDirectoryContent)
      )
  );

  Widget get _configuration => Obx(() => SettingTile(
    title: translations.authenticatorConfigurationName,
    subtitle: translations.authenticatorConfigurationDescription,
    content: const ServerTypeSelector(
        authenticator: true
    ),
    expandedContent: [
      if(_authenticatorController.type.value == ServerType.remote)
        SettingTile(
            title: translations.authenticatorConfigurationHostName,
            subtitle: translations.authenticatorConfigurationHostDescription,
            isChild: true,
            content: TextFormBox(
                placeholder: translations.authenticatorConfigurationHostName,
                controller: _authenticatorController.host
            )
        ),
      if(_authenticatorController.type.value != ServerType.embedded)
        SettingTile(
            title: translations.authenticatorConfigurationPortName,
            subtitle: translations.authenticatorConfigurationPortDescription,
            isChild: true,
            content: TextFormBox(
                placeholder: translations.authenticatorConfigurationPortName,
                controller: _authenticatorController.port,
                keyboardType: TextInputType.number,
                inputFormatters: [
                  FilteringTextInputFormatter.digitsOnly
                ]
            )
        ),
      if(_authenticatorController.type.value == ServerType.embedded)
        SettingTile(
            title: translations.authenticatorConfigurationDetachedName,
            subtitle: translations.authenticatorConfigurationDetachedDescription,
            contentWidth: null,
            isChild: true,
            content: Obx(() => Row(
              children: [
                Text(
                    _authenticatorController.detached.value ? translations.on : translations.off
                ),
                const SizedBox(
                    width: 16.0
                ),
                ToggleSwitch(
                    checked: _authenticatorController.detached(),
                    onChanged: (value) => _authenticatorController.detached.value = value
                ),
              ],
            ))
        )
    ],
  ));
}
