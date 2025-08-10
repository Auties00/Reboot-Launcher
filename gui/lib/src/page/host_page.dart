import 'package:clipboard/clipboard.dart';
import 'package:dart_ipify/dart_ipify.dart';
import 'package:fluent_ui/fluent_ui.dart' as fluentUi show FluentIcons;
import 'package:fluent_ui/fluent_ui.dart' hide FluentIcons;
import 'package:fluentui_system_icons/fluentui_system_icons.dart';
import 'package:flutter/services.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/main.dart';
import 'package:reboot_launcher/src/controller/dll_controller.dart';
import 'package:reboot_launcher/src/controller/hosting_controller.dart';
import 'package:reboot_launcher/src/controller/server_browser_controller.dart';
import 'package:reboot_launcher/src/pager/page_type.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/tile/setting_tile.dart';
import 'package:reboot_launcher/src/message/data.dart';
import 'package:reboot_launcher/src/messenger/info_bar.dart';
import 'package:reboot_launcher/src/messenger/overlay.dart';
import 'package:reboot_launcher/src/pager/abstract_page.dart';
import 'package:reboot_launcher/src/button/game_start_button.dart';
import 'package:reboot_launcher/src/button/version_selector.dart';

final GlobalKey<OverlayTargetState> hostVersionOverlayTargetKey = GlobalKey();
final GlobalKey<OverlayTargetState> hostInfoOverlayTargetKey = GlobalKey();
final GlobalKey<OverlayTargetState> hostInfoNameOverlayTargetKey = GlobalKey();
final GlobalKey<OverlayTargetState> hostInfoDescriptionOverlayTargetKey = GlobalKey();
final GlobalKey<OverlayTargetState> hostInfoPasswordOverlayTargetKey = GlobalKey();
final GlobalKey<OverlayTargetState> hostShareOverlayTargetKey = GlobalKey();
final GlobalKey<SettingTileState> hostInfoTileKey = GlobalKey();

class HostPage extends AbstractPage {
  const HostPage({Key? key}) : super(key: key);

  @override
  String get name => translations.hostName;

  @override
  String get iconAsset => "assets/images/host.png";

  @override
  PageType get type => PageType.host;

  @override
  bool hasButton(String? pageName) => pageName == null;

  @override
  AbstractPageState<HostPage> createState() => _HostingPageState();
}

class _HostingPageState extends AbstractPageState<HostPage> {
  final ServerBrowserController _serverBrowserController = Get.find<ServerBrowserController>();
  final HostingController _hostingController = Get.find<HostingController>();
  final DllController _dllController = Get.find<DllController>();

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
  Widget get button => LaunchButton(
      host: true,
      startLabel: translations.startHosting,
      stopLabel: translations.stopHosting
  );

  @override
  List<Widget> get settings => [
    _information,
    VersionSelector.buildTile(
        key: hostVersionOverlayTargetKey
    ),
    _options,
    _share,
    _resetDefaults
  ];


  SettingTile get _information => SettingTile(
      key: hostInfoTileKey,
      icon: Icon(
          FluentIcons.info_24_regular
      ),
      title: Text(translations.hostGameServerName),
      subtitle: Text(translations.hostGameServerDescription),
      overlayKey: hostInfoOverlayTargetKey,
      children: [
        SettingTile(
            icon: Icon(
                FluentIcons.textbox_24_regular
            ),
            title: Text(translations.hostGameServerNameName),
            subtitle: Text(translations.hostGameServerNameDescription),
            content: OverlayTarget(
              key: hostInfoNameOverlayTargetKey,
              child: TextFormBox(
                  placeholder: translations.hostGameServerNameName,
                  controller: _hostingController.name,
                  focusNode: _hostingController.nameFocusNode,
                  onChanged: (_) => _updateServer()
              ),
            )
        ),
        SettingTile(
            icon: Icon(
                FluentIcons.text_description_24_regular
            ),
            title: Text(translations.hostGameServerDescriptionName),
            subtitle: Text(translations.hostGameServerDescriptionDescription),
            content: OverlayTarget(
              key: hostInfoDescriptionOverlayTargetKey,
              child: TextFormBox(
                  placeholder: translations.hostGameServerDescriptionName,
                  controller: _hostingController.description,
                  focusNode: _hostingController.descriptionFocusNode,
                  onChanged: (_) => _updateServer()
              ),
            )
        ),
        SettingTile(
            icon: Icon(
                FluentIcons.password_24_regular
            ),
            title: Text(translations.hostGameServerPasswordName),
            subtitle: Text(translations.hostGameServerPasswordDescription),
            content: Obx(() => OverlayTarget(
              key: hostInfoPasswordOverlayTargetKey,
              child: TextFormBox(
                  placeholder: translations.hostGameServerPasswordName,
                  controller: _hostingController.password,
                  focusNode: _hostingController.passwordFocusNode,
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
                        shape: WidgetStateProperty.all(const CircleBorder()),
                        backgroundColor: WidgetStateProperty.all(Colors.transparent)
                    ),
                    child: Icon(
                        _hostingController.showPassword.value ? FluentIcons.eye_off_24_filled : FluentIcons.eye_24_filled,
                        color: _showPasswordTrailing.value ? null : Colors.transparent
                    ),
                  )
              ),
            ))
        )
      ]
  );

  SettingTile get _options => SettingTile(
    icon: Icon(
        FluentIcons.options_24_regular
    ),
    title: Text(translations.settingsServerOptionsName),
    subtitle: Text(translations.settingsServerOptionsSubtitle),
    children: [
      SettingTile(
          icon: Icon(
              FluentIcons.options_24_regular
          ),
          title: Text(translations.settingsClientArgsName),
          subtitle: Text(translations.settingsClientArgsDescription),
          content: TextFormBox(
            placeholder: translations.settingsClientArgsPlaceholder,
            controller: _hostingController.customLaunchArgs,
          )
      ),
      SettingTile(
        icon: Icon(
            FluentIcons.window_console_20_regular
        ),
        title: Text(translations.gameServerTypeName),
        subtitle: Text(translations.gameServerTypeDescription),
        contentWidth: null,
        content: Row(
          children: [
            Obx(() => Text(
                _hostingController.headless.value ? translations.on : translations.off
            )),
            const SizedBox(
                width: 16.0
            ),
            Obx(() => ToggleSwitch(
                checked: _hostingController.headless.value,
                onChanged: (value) => _hostingController.headless.value = value
            )),
          ],
        ),
      ),
      SettingTile(
        icon: Icon(
            FluentIcons.arrow_reset_24_regular
        ),
        title: Text(translations.hostAutomaticRestartName),
        subtitle: Text(translations.hostAutomaticRestartDescription),
        contentWidth: null,
        content: Row(
          children: [
            Obx(() => Text(
                _hostingController.autoRestart.value ? translations.on : translations.off
            )),
            const SizedBox(
                width: 16.0
            ),
            Obx(() => ToggleSwitch(
                checked: _hostingController.autoRestart.value,
                onChanged: (value) => _hostingController.autoRestart.value = value
            )),
          ],
        ),
      ),
      SettingTile(
          icon: Icon(
              fluentUi.FluentIcons.number_field
          ),
          title: Text(translations.settingsServerPortName),
          subtitle: Text(translations.settingsServerPortDescription),
          contentWidth: 64,
          content: TextFormBox(
              placeholder:  translations.settingsServerPortName,
              controller: _dllController.gameServerPort,
              keyboardType: TextInputType.number,
              textAlign: TextAlign.center,
              inputFormatters: [
                FilteringTextInputFormatter.digitsOnly
              ]
          )
      )
    ],
  );

  SettingTile get _share => SettingTile(
    icon: Icon(
        FluentIcons.link_24_regular
    ),
    title: Text(translations.hostShareName),
    subtitle: Text(translations.hostShareDescription),
    overlayKey: hostShareOverlayTargetKey,
    children: [
      SettingTile(
          icon: Icon(
              FluentIcons.link_24_regular
          ),
          title: Text(translations.hostShareLinkName),
          subtitle: Text(translations.hostShareLinkDescription),
          content: Button(
            onPressed: () async {
              FlutterClipboard.controlC("$kCustomUrlSchema://${_hostingController.uuid}");
              _showCopiedLink();
            },
            child: Text(translations.hostShareLinkContent),
          )
      ),
      SettingTile(
          icon: Icon(
              FluentIcons.globe_24_regular
          ),
          title: Text(translations.hostShareIpName),
          subtitle: Text(translations.hostShareIpDescription),
          content: Button(
            onPressed: () async {
              InfoBarEntry? entry;
              try {
                entry = _showCopyingIp();
                final ip = await Ipify.ipv4();
                entry.close();
                FlutterClipboard.controlC(ip);
                _showCopiedIp();
              }catch(error) {
                entry?.close();
                _showCannotCopyIp(error);
              }
            },
            child: Text(translations.hostShareIpContent),
          )
      )
    ],
  );

  SettingTile get _resetDefaults => SettingTile(
      icon: Icon(
          FluentIcons.arrow_reset_24_regular
      ),
      title: Text(translations.hostResetName),
      subtitle: Text(translations.hostResetDescription),
      content: Button(
        onPressed: () => showResetDialog(() {
          _hostingController.reset();
          _dllController.resetServer();
        }),
        child: Text(translations.hostResetContent),
      )
  );

  Future<void> _updateServer() async {
    try {
      final server = await _hostingController.createServerBrowserEntry();
      _serverBrowserController.addServer(server);
    } catch(error) {
      _showCannotUpdateGameServer(error);
    }
  }

  void _showCopiedLink() => showRebootInfoBar(
      translations.hostShareLinkMessageSuccess,
      severity: InfoBarSeverity.success
  );

  InfoBarEntry _showCopyingIp() => showRebootInfoBar(
      translations.hostShareIpMessageLoading,
      loading: true,
      duration: null
  );

  void _showCopiedIp() => showRebootInfoBar(
      translations.hostShareIpMessageSuccess,
      severity: InfoBarSeverity.success
  );

  void _showCannotCopyIp(Object error) => showRebootInfoBar(
      translations.hostShareIpMessageError(error.toString()),
      severity: InfoBarSeverity.error,
      duration: infoBarLongDuration
  );

  void _showCannotUpdateGameServer(Object error) => showRebootInfoBar(
      translations.cannotUpdateGameServer(error.toString()),
      severity: InfoBarSeverity.success,
      duration: infoBarLongDuration
  );
}