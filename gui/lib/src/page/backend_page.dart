import 'package:fluent_ui/fluent_ui.dart' as fluentUi show FluentIcons;
import 'package:fluent_ui/fluent_ui.dart' hide FluentIcons;
import 'package:fluentui_system_icons/fluentui_system_icons.dart';
import 'package:flutter/services.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/backend_controller.dart';
import 'package:reboot_launcher/src/pager/page_type.dart';
import 'package:reboot_launcher/src/util/keyboard.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/tile/setting_tile.dart';
import 'package:reboot_launcher/src/message/data.dart';
import 'package:reboot_launcher/src/messenger/info_bar.dart';
import 'package:reboot_launcher/src/messenger/overlay.dart';
import 'package:reboot_launcher/src/pager/abstract_page.dart';
import 'package:reboot_launcher/src/button/backend_start_button.dart';
import 'package:reboot_launcher/src/button/server_type_selector.dart';
import 'package:url_launcher/url_launcher.dart';

final GlobalKey<OverlayTargetState> backendTypeOverlayTargetKey = GlobalKey();
final GlobalKey<OverlayTargetState> backendGameServerAddressOverlayTargetKey = GlobalKey();
final GlobalKey<OverlayTargetState> backendUnrealEngineOverlayTargetKey = GlobalKey();
final GlobalKey<OverlayTargetState> backendDetachedOverlayTargetKey = GlobalKey();

class BackendPage extends AbstractPage {
  const BackendPage({Key? key}) : super(key: key);

  @override
  String get name => translations.backendName;

  @override
  String get iconAsset => "assets/images/backend.png";

  @override
  PageType get type => PageType.backend;

  @override
  bool hasButton(String? pageName) => pageName == null;

  @override
  AbstractPageState<BackendPage> createState() => _BackendPageState();
}

class _BackendPageState extends AbstractPageState<BackendPage> {
  final BackendController _backendController = Get.find<BackendController>();

  InfoBarEntry? _infoBarEntry;

  @override
  void initState() {
    ServicesBinding.instance.keyboard.addHandler((keyEvent) {
      if(_infoBarEntry == null) {
        return false;
      }

      if(keyEvent.physicalKey.isUnrealEngineKey) {
        _backendController.consoleKey.value = keyEvent.physicalKey;
      }

      _infoBarEntry?.close();
      _infoBarEntry = null;
      return true;
    });
    super.initState();
  }

  @override
  List<Widget> get settings => [
    _type,
    _hostName,
    _port,
    _gameServerAddress,
    _unrealEngineConsoleKey,
    _detached,
    _installationDirectory,
    _resetDefaults
  ];

  Widget get _gameServerAddress => Obx(() {
    if(_backendController.type.value != AuthBackendType.embedded) {
      return const SizedBox.shrink();
    }

    return SettingTile(
        icon: Icon(
            FluentIcons.stream_input_20_regular
        ),
        title: Text(translations.matchmakerConfigurationAddressName),
        subtitle: Text(translations.matchmakerConfigurationAddressDescription),
        content: OverlayTarget(
          key: backendGameServerAddressOverlayTargetKey,
          child: TextFormBox(
              placeholder: translations.matchmakerConfigurationAddressName,
              controller: _backendController.gameServerAddress,
              focusNode: _backendController.gameServerAddressFocusNode
          ),
        )
    );
  });

  Widget get _hostName => Obx(() {
    if(_backendController.type.value != AuthBackendType.remote) {
      return const SizedBox.shrink();
    }

    return SettingTile(
        icon: Icon(
            FluentIcons.globe_24_regular
        ),
        title: Text(translations.backendConfigurationHostName),
        subtitle: Text(translations.backendConfigurationHostDescription),
        content: TextFormBox(
            placeholder: translations.backendConfigurationHostName,
            controller: _backendController.host
        )
    );
  });

  Widget get _port => Obx(() {
    if(_backendController.type.value == AuthBackendType.embedded) {
      return const SizedBox.shrink();
    }

    return SettingTile(
        icon: Icon(
            fluentUi.FluentIcons.number_field
        ),
        title: Text(translations.backendConfigurationPortName),
        subtitle: Text(translations.backendConfigurationPortDescription),
        content: TextFormBox(
            placeholder: translations.backendConfigurationPortName,
            controller: _backendController.port,
            keyboardType: TextInputType.number,
            inputFormatters: [
              FilteringTextInputFormatter.digitsOnly
            ]
        )
    );
  });

  Widget get _detached => Obx(() {
    if(_backendController.type.value != AuthBackendType.embedded) {
      return const SizedBox.shrink();
    }

    return SettingTile(
        icon: Icon(
            FluentIcons.developer_board_24_regular
        ),
        title: Text(translations.backendConfigurationDetachedName),
        subtitle: Text(translations.backendConfigurationDetachedDescription),
        contentWidth: null,
        content: Row(
          children: [
            Obx(() => Text(
                _backendController.detached.value ? translations.on : translations.off
            )),
            const SizedBox(
                width: 16.0
            ),
            OverlayTarget(
              key: backendDetachedOverlayTargetKey,
              child: ToggleSwitch(
                  checked: _backendController.detached(),
                  onChanged: (value) async => _backendController.detached.value = value
              ),
            ),
          ],
        )
    );
  });

  Widget get _unrealEngineConsoleKey => Obx(() {
    if(_backendController.type.value != AuthBackendType.embedded) {
      return const SizedBox.shrink();
    }

    return SettingTile(
      icon: Icon(
          FluentIcons.key_24_regular
      ),
      title: Text(translations.settingsClientConsoleKeyName),
      subtitle: Text(translations.settingsClientConsoleKeyDescription),
      contentWidth: null,
      content: OverlayTarget(
        key: backendUnrealEngineOverlayTargetKey,
        child: Button(
          onPressed: () {
            _infoBarEntry = showRebootInfoBar(
                translations.clickKey,
                loading: true,
                duration: null
            );
          },
          child: Text(_backendController.consoleKey.value.unrealEnginePrettyName ?? ""),
        ),
      )
    );
  });

  SettingTile get _resetDefaults => SettingTile(
      icon: Icon(
          FluentIcons.arrow_reset_24_regular
      ),
      title: Text(translations.backendResetDefaultsName),
      subtitle: Text(translations.backendResetDefaultsDescription),
      content: Button(
        onPressed: () => showResetDialog(_backendController.reset),
        child: Text(translations.backendResetDefaultsContent),
      )
  );

  Widget get _installationDirectory => Obx(() {
    if(_backendController.type.value != AuthBackendType.embedded) {
      return const SizedBox.shrink();
    }

    return SettingTile(
        icon: Icon(
            FluentIcons.folder_24_regular
        ),
        title: Text(translations.backendInstallationDirectoryName),
        subtitle: Text(translations.backendInstallationDirectoryDescription),
        content: Button(
            onPressed: () => launchUrl(backendDirectory.uri),
            child: Text(translations.backendInstallationDirectoryContent)
        )
    );
  });

  Widget get _type => SettingTile(
      icon: Icon(
          FluentIcons.password_24_regular
      ),
      title: Text(translations.backendTypeName),
      subtitle: Text(translations.backendTypeDescription),
      content: ServerTypeSelector(
        overlayKey: backendTypeOverlayTargetKey
      )
  );

  @override
  Widget get button => const BackendButton();
}
