import 'package:fluent_ui/fluent_ui.dart' as fluentUi show FluentIcons;
import 'package:fluent_ui/fluent_ui.dart' hide FluentIcons;
import 'package:fluentui_system_icons/fluentui_system_icons.dart';
import 'package:flutter/services.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/authenticator_controller.dart';
import 'package:reboot_launcher/src/page/abstract/page.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/widget/server_start_button.dart';
import 'package:reboot_launcher/src/widget/server_type_selector.dart';
import 'package:reboot_launcher/src/widget/setting_tile.dart';
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
  bool hasButton(String? pageName) => pageName == null;

  @override
  RebootPageState<AuthenticatorPage> createState() => _AuthenticatorPageState();
}

class _AuthenticatorPageState extends RebootPageState<AuthenticatorPage> {
  final AuthenticatorController _authenticatorController = Get.find<AuthenticatorController>();

  @override
  List<Widget> get settings => [
    _type,
    _hostName,
    _port,
    _detached,
    _installationDirectory,
    _resetDefaults
  ];

  Widget get _hostName => Obx(() {
    if(_authenticatorController.type.value != ServerType.remote) {
      return const SizedBox.shrink();
    }

    return SettingTile(
        icon: Icon(
            FluentIcons.globe_24_regular
        ),
        title: Text(translations.authenticatorConfigurationHostName),
        subtitle: Text(translations.authenticatorConfigurationHostDescription),
        content: TextFormBox(
            placeholder: translations.authenticatorConfigurationHostName,
            controller: _authenticatorController.host
        )
    );
  });

  Widget get _port => Obx(() {
    if(_authenticatorController.type.value == ServerType.embedded) {
      return const SizedBox.shrink();
    }

    return SettingTile(
        icon: Icon(
            fluentUi.FluentIcons.number_field
        ),
        title: Text(translations.authenticatorConfigurationPortName),
        subtitle: Text(translations.authenticatorConfigurationPortDescription),
        content: TextFormBox(
            placeholder: translations.authenticatorConfigurationPortName,
            controller: _authenticatorController.port,
            keyboardType: TextInputType.number,
            inputFormatters: [
              FilteringTextInputFormatter.digitsOnly
            ]
        )
    );
  });

  Widget get _detached => Obx(() {
    if(_authenticatorController.type.value != ServerType.embedded) {
      return const SizedBox.shrink();
    }

    return SettingTile(
        icon: Icon(
            FluentIcons.developer_board_24_regular
        ),
        title: Text(translations.authenticatorConfigurationDetachedName),
        subtitle: Text(translations.authenticatorConfigurationDetachedDescription),
        contentWidth: null,
        content: Row(
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
        )
    );
  });

  SettingTile get _resetDefaults => SettingTile(
      icon: Icon(
          FluentIcons.arrow_reset_24_regular
      ),
      title: Text(translations.authenticatorResetDefaultsName),
      subtitle: Text(translations.authenticatorResetDefaultsDescription),
      content: Button(
        onPressed: () => showResetDialog(_authenticatorController.reset),
        child: Text(translations.authenticatorResetDefaultsContent),
      )
  );

  Widget get _installationDirectory => Obx(() {
    if(_authenticatorController.type.value != ServerType.embedded) {
      return const SizedBox.shrink();
    }

    return SettingTile(
        icon: Icon(
            FluentIcons.folder_24_regular
        ),
        title: Text(translations.authenticatorInstallationDirectoryName),
        subtitle: Text(translations.authenticatorInstallationDirectoryDescription),
        content: Button(
            onPressed: () => launchUrl(authenticatorDirectory.uri),
            child: Text(translations.authenticatorInstallationDirectoryContent)
        )
    );
  });

  Widget get _type => SettingTile(
      icon: Icon(
          FluentIcons.password_24_regular
      ),
      title: Text(translations.authenticatorTypeName),
      subtitle: Text(translations.authenticatorTypeDescription),
      content: const ServerTypeSelector(
          authenticator: true
      )
  );

  @override
  Widget get button => const ServerButton(
      authenticator: true
  );
}
