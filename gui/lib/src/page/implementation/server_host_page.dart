import 'package:clipboard/clipboard.dart';
import 'package:dart_ipify/dart_ipify.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/material.dart' show Icons;
import 'package:get/get.dart';
import 'package:reboot_launcher/main.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/hosting_controller.dart';
import 'package:reboot_launcher/src/dialog/implementation/data.dart';
import 'package:reboot_launcher/src/dialog/implementation/server.dart';
import 'package:reboot_launcher/src/page/abstract/page.dart';
import 'package:reboot_launcher/src/page/abstract/page_setting.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/widget/common/setting_tile.dart';
import 'package:reboot_launcher/src/widget/game/start_button.dart';
import 'package:reboot_launcher/src/widget/version/version_selector_tile.dart';
import 'package:sync/semaphore.dart';

class HostPage extends RebootPage {
  const HostPage({Key? key}) : super(key: key);

  @override
  String get name => "Host";

  @override
  String get iconAsset => "assets/images/host.png";

  @override
  RebootPageType get type => RebootPageType.host;

  @override
  bool get hasButton => true;

  @override
  RebootPageState<HostPage> createState() => _HostingPageState();

  @override
  List<PageSetting> get settings => [
    PageSetting(
      name: translations.hostGameServerName,
      description: translations.hostGameServerDescription,
      children: [
        PageSetting(
            name: translations.hostGameServerNameName,
            description: translations.hostGameServerNameDescription
        ),
        PageSetting(
            name: translations.hostGameServerDescriptionName,
            description: translations.hostGameServerDescriptionDescription
        ),
        PageSetting(
            name: translations.hostGameServerPasswordName,
            description: translations.hostGameServerDescriptionDescription
        ),
        PageSetting(
            name: translations.hostGameServerDiscoverableName,
            description: translations.hostGameServerDiscoverableDescription
        )
      ],
    ),
    versionSelectorRebootSetting,
    PageSetting(
      name: translations.hostShareName,
      description: translations.hostShareDescription,
      children: [
        PageSetting(
            name: translations.hostShareLinkName,
            description: translations.hostShareLinkDescription,
            content: translations.hostShareLinkContent
        ),
        PageSetting(
            name: translations.hostShareIpName,
            description: translations.hostShareIpDescription,
            content: translations.hostShareIpContent
        )
      ],
    ),
    PageSetting(
        name: translations.hostResetName,
        description: translations.hostResetDescription,
        content: translations.hostResetContent
    )
  ];
}

class _HostingPageState extends RebootPageState<HostPage> {
  final GameController _gameController = Get.find<GameController>();
  final HostingController _hostingController = Get.find<HostingController>();
  final Semaphore _semaphore = Semaphore();

  late final RxBool _showPasswordTrailing = RxBool(_hostingController.password.text.isNotEmpty);

  @override
  void initState() {
    if(_hostingController.name.text.isEmpty) {
      _hostingController.name.text = translations.defaultServerName;
    }

    if(_hostingController.description.text.isEmpty) {
      _hostingController.description.text = translations.defaultServerDescription;
    }

    super.initState();
  }

  @override
  Widget get button => const LaunchButton(
      host: true
  );

  @override
  List<SettingTile> get settings => [
    _gameServer,
    versionSelectorSettingTile,
    _share,
    _resetDefaults
  ];

  SettingTile get _resetDefaults => SettingTile(
      title: translations.hostResetName,
      subtitle: translations.hostResetDescription,
      content: Button(
        onPressed: () => showResetDialog(_hostingController.reset),
        child: Text(translations.hostResetContent),
      )
  );

  SettingTile get _gameServer => SettingTile(
      title: translations.hostGameServerName,
      subtitle: translations.hostGameServerDescription,
      expandedContent: [
        SettingTile(
            title: translations.hostGameServerNameName,
            subtitle: translations.hostGameServerNameDescription,
            isChild: true,
            content: TextFormBox(
                placeholder: translations.hostGameServerNameName,
                controller: _hostingController.name,
                onChanged: (_) => _updateServer()
            )
        ),
        SettingTile(
            title: translations.hostGameServerDescriptionName,
            subtitle: translations.hostGameServerDescriptionDescription,
            isChild: true,
            content: TextFormBox(
                placeholder: translations.hostGameServerDescriptionName,
                controller: _hostingController.description,
                onChanged: (_) => _updateServer()
            )
        ),
        SettingTile(
            title: translations.hostGameServerPasswordName,
            subtitle: translations.hostGameServerDescriptionDescription,
            isChild: true,
            content: Obx(() => TextFormBox(
                placeholder: translations.hostGameServerPasswordName,
                controller: _hostingController.password,
                autovalidateMode: AutovalidateMode.always,
                obscureText: !_hostingController.showPassword.value,
                enableSuggestions: false,
                autocorrect: false,
                onChanged: (text) {
                  _showPasswordTrailing.value = text.isNotEmpty;
                  _updateServer();
                },
                suffix: Button(
                  onPressed: () => _hostingController.showPassword.value = !_hostingController.showPassword.value,
                  style: ButtonStyle(
                      shape: ButtonState.all(const CircleBorder()),
                      backgroundColor: ButtonState.all(Colors.transparent)
                  ),
                  child: Icon(
                      _hostingController.showPassword.value ? Icons.visibility_off : Icons.visibility,
                      color: _showPasswordTrailing.value ? null : Colors.transparent
                  ),
                )
            ))
        ),
        SettingTile(
            title: translations.hostGameServerDiscoverableName,
            subtitle: translations.hostGameServerDiscoverableDescription,
            isChild: true,
            contentWidth: null,
            content: Obx(() => Row(
              children: [
                Text(
                    _hostingController.discoverable.value ? translations.on : translations.off
                ),
                const SizedBox(
                    width: 16.0
                ),
                ToggleSwitch(
                    checked: _hostingController.discoverable(),
                    onChanged: (value) async {
                      _hostingController.discoverable.value = value;
                      await _updateServer();
                    }
                ),
              ],
            ))
        )
      ]
  );

  SettingTile get _share => SettingTile(
    title: translations.hostShareName,
    subtitle: translations.hostShareDescription,
    expandedContent: [
      SettingTile(
          title: translations.hostShareLinkName,
          subtitle: translations.hostShareLinkDescription,
          isChild: true,
          content: Button(
            onPressed: () async {
              FlutterClipboard.controlC("$kCustomUrlSchema://${_hostingController.uuid}");
              _showCopiedLink();
            },
            child: Text(translations.hostShareLinkContent),
          )
      ),
      SettingTile(
          title: translations.hostShareIpName,
          subtitle: translations.hostShareIpDescription,
          isChild: true,
          content: Button(
            onPressed: () async {
              try {
                _showCopyingIp();
                var ip = await Ipify.ipv4();
                FlutterClipboard.controlC(ip);
               _showCopiedIp();
              }catch(error) {
                _showCannotCopyIp(error);
              }
            },
            child: Text(translations.hostShareIpContent),
          )
      )
    ],
  );

  Future<void> _updateServer() async {
    if(!_hostingController.published()) {
      return;
    }

    try {
      _semaphore.acquire();
      _hostingController.publishServer(
          _gameController.username.text,
          _hostingController.instance.value!.versionName
      );
    } catch(error) {
      _showCannotUpdateGameServer(error);
    } finally {
      _semaphore.release();
    }
  }

  void _showCopiedLink() => showInfoBar(
      translations.hostShareLinkMessageSuccess,
      severity: InfoBarSeverity.success
  );

  void _showCopyingIp() => showInfoBar(
      translations.hostShareIpMessageLoading,
      loading: true,
      duration: null
  );

  void _showCopiedIp() => showInfoBar(
      translations.hostShareIpMessageSuccess,
      severity: InfoBarSeverity.success
  );

  void _showCannotCopyIp(Object error) => showInfoBar(
      translations.hostShareIpMessageError(error.toString()),
      severity: InfoBarSeverity.error,
      duration: snackbarLongDuration
  );

  void _showCannotUpdateGameServer(Object error) => showInfoBar(
      translations.cannotUpdateGameServer(error.toString()),
      severity: InfoBarSeverity.success,
      duration: snackbarLongDuration
  );
}