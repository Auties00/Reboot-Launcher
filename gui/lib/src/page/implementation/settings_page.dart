import 'package:fluent_ui/fluent_ui.dart' as fluentUi show FluentIcons;
import 'package:fluent_ui/fluent_ui.dart' hide FluentIcons;
import 'package:fluentui_system_icons/fluentui_system_icons.dart';
import 'package:flutter/services.dart';
import 'package:flutter_gen/gen_l10n/reboot_localizations.dart';
import 'package:flutter_localized_locales/flutter_localized_locales.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/hosting_controller.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/controller/update_controller.dart';
import 'package:reboot_launcher/src/dialog/abstract/info_bar.dart';
import 'package:reboot_launcher/src/dialog/implementation/data.dart';
import 'package:reboot_launcher/src/page/abstract/page.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/util/checks.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/widget/file_selector.dart';
import 'package:reboot_launcher/src/widget/setting_tile.dart';
import 'package:url_launcher/url_launcher.dart';

class SettingsPage extends RebootPage {
  const SettingsPage({Key? key}) : super(key: key);

  @override
  String get name => translations.settingsName;

  @override
  String get iconAsset => "assets/images/settings.png";

  @override
  RebootPageType get type => RebootPageType.settings;

  @override
  bool hasButton(String? pageName) => false;

  @override
  RebootPageState<SettingsPage> createState() => _SettingsPageState();
}

class _SettingsPageState extends RebootPageState<SettingsPage> {
  final GameController _gameController  = Get.find<GameController>();
  final HostingController _hostingController = Get.find<HostingController>();
  final SettingsController _settingsController = Get.find<SettingsController>();
  final UpdateController _updateController = Get.find<UpdateController>();

  @override
  Widget? get button => null;

  @override
  List<Widget> get settings => [
    _clientSettings,
    _gameServerSettings,
    _launcherSettings,
    _installationDirectory
  ];

  SettingTile get _installationDirectory => SettingTile(
      icon: Icon(
          FluentIcons.folder_24_regular
      ),
      title: Text(translations.settingsUtilsInstallationDirectoryName),
      subtitle: Text(translations.settingsUtilsInstallationDirectorySubtitle),
      content: Button(
        onPressed: () => launchUrl(installationDirectory.uri),
        child: Text(translations.settingsUtilsInstallationDirectoryContent),
      )
  );

  SettingTile get _clientSettings => SettingTile(
    icon: Icon(
        FluentIcons.desktop_24_regular
    ),
    title: Text(translations.settingsClientName),
    subtitle: Text(translations.settingsClientDescription),
    children: [
      _createFileSetting(
          title: translations.settingsClientConsoleName,
          description: translations.settingsClientConsoleDescription,
          controller: _settingsController.unrealEngineConsoleDll
      ),
      _createFileSetting(
          title: translations.settingsClientAuthName,
          description: translations.settingsClientAuthDescription,
          controller: _settingsController.authenticatorDll
      ),
      _createFileSetting(
          title: translations.settingsClientMemoryName,
          description: translations.settingsClientMemoryDescription,
          controller: _settingsController.memoryLeakDll
      ),
      SettingTile(
          icon: Icon(
              FluentIcons.text_box_settings_24_regular
          ),
          title: Text(translations.settingsClientArgsName),
          subtitle: Text(translations.settingsClientArgsDescription),
          content: TextFormBox(
            placeholder: translations.settingsClientArgsPlaceholder,
            controller: _gameController.customLaunchArgs,
          )
      ),
    ],
  );

  SettingTile get _gameServerSettings => SettingTile(
    icon: Icon(
        FluentIcons.server_24_regular
    ),
    title: Text(translations.settingsServerName),
    subtitle: Text(translations.settingsServerSubtitle),
    children: [
      _createFileSetting(
          title: translations.settingsServerFileName,
          description: translations.settingsServerFileDescription,
          controller: _settingsController.gameServerDll
      ),
      SettingTile(
          icon: Icon(
              fluentUi.FluentIcons.number_field
          ),
          title: Text(translations.settingsServerPortName),
          subtitle: Text(translations.settingsServerPortDescription),
          content: TextFormBox(
              placeholder:  translations.settingsServerPortName,
              controller: _settingsController.gameServerPort,
              keyboardType: TextInputType.number,
              inputFormatters: [
                FilteringTextInputFormatter.digitsOnly
              ]
          )
      ),
      SettingTile(
          icon: Icon(
              FluentIcons.globe_24_regular
          ),
          title: Text(translations.settingsServerMirrorName),
          subtitle: Text(translations.settingsServerMirrorDescription),
          content: TextFormBox(
              placeholder:  translations.settingsServerMirrorPlaceholder,
              controller: _updateController.url,
              validator: checkUpdateUrl
          )       
      ),
      SettingTile(
          icon: Icon(
              FluentIcons.timer_24_regular
          ),
          title: Text(translations.settingsServerTimerName),
          subtitle: Text(translations.settingsServerTimerSubtitle),
          content: Obx(() => DropDownButton(
              leading: Text(_updateController.timer.value.text),
              items: UpdateTimer.values.map((entry) => MenuFlyoutItem(
                  text: Text(entry.text),
                  onPressed: () {
                    _updateController.timer.value = entry;
                    removeMessageByPage(6);
                    _updateController.update(true);
                  }
              )).toList()
          ))       
      ),
      SettingTile(
        icon: Icon(
            FluentIcons.developer_board_24_regular
        ),
        title: Text(translations.playAutomaticServerName),
        subtitle: Text(translations.playAutomaticServerDescription),
        contentWidth: null,
        content: Obx(() => Row(
          children: [
            Text(
                _hostingController.automaticServer.value ? translations.on : translations.off
            ),
            const SizedBox(
                width: 16.0
            ),
            ToggleSwitch(
                checked: _hostingController.automaticServer.value,
                onChanged: (value) => _hostingController.automaticServer.value = value
            ),
          ],
        )),
      )
    ],
  );

  SettingTile get _launcherSettings => SettingTile(
    icon: Icon(
        FluentIcons.play_24_regular
    ),
    title: Text(translations.settingsUtilsName),
    subtitle: Text(translations.settingsUtilsSubtitle),
    children: [
      SettingTile(
          icon: Icon(
              FluentIcons.local_language_24_regular
          ),
          title: Text(translations.settingsUtilsLanguageName),
          subtitle: Text(translations.settingsUtilsLanguageDescription),
          content: Obx(() => DropDownButton(
              leading: Text(_getLocaleName(_settingsController.language.value)),
              items: AppLocalizations.supportedLocales.map((locale) => MenuFlyoutItem(
                  text: Text(_getLocaleName(locale.languageCode)),
                  onPressed: () => _settingsController.language.value = locale.languageCode
              )).toList()
          ))
      ),
      SettingTile(
          icon: Icon(
              FluentIcons.dark_theme_24_regular
          ),
          title: Text(translations.settingsUtilsThemeName),
          subtitle: Text(translations.settingsUtilsThemeDescription),
          content: Obx(() => DropDownButton(
              leading: Text(_settingsController.themeMode.value.title),
              items: ThemeMode.values.map((themeMode) => MenuFlyoutItem(
                  text: Text(themeMode.title),
                  onPressed: () => _settingsController.themeMode.value = themeMode
              )).toList()
          ))
      ),
      SettingTile(
          icon: Icon(
              FluentIcons.arrow_reset_24_regular
          ),
          title: Text(translations.settingsUtilsResetDefaultsName),
          subtitle: Text(translations.settingsUtilsResetDefaultsSubtitle),
          content: Button(
            onPressed: () => showResetDialog(_settingsController.reset),
            child: Text(translations.settingsUtilsResetDefaultsContent),
          )
      )
    ],
  );

  String _getLocaleName(String locale) {
    var result = LocaleNames.of(context)!.nameOf(locale);
    if(result != null) {
      return "${result.substring(0, 1).toUpperCase()}${result.substring(1).toLowerCase()}";
    }

    return locale;
  }

  SettingTile _createFileSetting({required String title, required String description, required TextEditingController controller}) => SettingTile(
      icon: Icon(
        FluentIcons.document_24_regular
      ),
      title: Text(title),
      subtitle: Text(description),
      content: FileSelector(
          placeholder: translations.selectPathPlaceholder,
          windowTitle: translations.selectPathWindowTitle,
          controller: controller,
          validator: checkDll,
          extension: "dll",
          folder: false
      )
  );
}

extension _UpdateTimerExtension on UpdateTimer {
  String get text {
    if (this == UpdateTimer.never) {
      return translations.updateGameServerDllNever;
    }

    return translations.updateGameServerDllEvery(name);
  }
}

extension _ThemeModeExtension on ThemeMode {
  String get title {
    switch(this) {
      case ThemeMode.system:
        return translations.system;
      case ThemeMode.dark:
        return translations.dark;
      case ThemeMode.light:
        return translations.light;
    }
  }
}