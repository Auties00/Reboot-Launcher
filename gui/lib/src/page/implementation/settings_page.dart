import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/services.dart';
import 'package:flutter_localized_locales/flutter_localized_locales.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/controller/update_controller.dart';
import 'package:reboot_launcher/src/dialog/abstract/info_bar.dart';
import 'package:reboot_launcher/src/dialog/implementation/data.dart';
import 'package:reboot_launcher/src/page/abstract/page.dart';
import 'package:reboot_launcher/src/page/abstract/page_setting.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/util/checks.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/widget/common/file_selector.dart';
import 'package:reboot_launcher/src/widget/common/setting_tile.dart';
import 'package:flutter_gen/gen_l10n/reboot_localizations.dart';
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
  bool get hasButton => false;

  @override
  RebootPageState<SettingsPage> createState() => _SettingsPageState();

  @override
  List<PageSetting> get settings => [
    PageSetting(
      name: translations.settingsClientName,
      description: translations.settingsClientDescription,
      children: [
        PageSetting(
            name: translations.settingsClientConsoleName,
            description: translations.settingsClientConsoleDescription
        ),
        PageSetting(
            name: translations.settingsClientAuthName,
            description: translations.settingsClientAuthDescription
        ),
        PageSetting(
            name: translations.settingsClientMemoryName,
            description: translations.settingsClientMemoryDescription
        ),
        PageSetting(
            name: translations.settingsClientArgsName,
            description: translations.settingsClientArgsDescription
        ),
      ],
    ),
    PageSetting(
      name: translations.settingsServerName,
      description: translations.settingsServerSubtitle,
      children: [
        PageSetting(
            name: translations.settingsServerFileName,
            description: translations.settingsServerFileDescription
        ),
        PageSetting(
            name: translations.settingsServerPortName,
            description: translations.settingsServerPortDescription
        ),
        PageSetting(
            name: translations.settingsServerMirrorName,
            description: translations.settingsServerMirrorDescription
        ),
        PageSetting(
            name: translations.settingsServerTimerName,
            description: translations.settingsServerTimerSubtitle
        ),
      ],
    ),
    PageSetting(
      name: translations.settingsUtilsName,
      description: translations.settingsUtilsSubtitle,
      children: [
        PageSetting(
            name: translations.settingsUtilsThemeName,
            description: translations.settingsUtilsThemeDescription,
        ),
        PageSetting(
          name: translations.settingsUtilsLanguageName,
          description: translations.settingsUtilsLanguageDescription,
        ),
        PageSetting(
            name: translations.settingsUtilsInstallationDirectoryName,
            description: translations.settingsUtilsInstallationDirectorySubtitle,
            content: translations.settingsUtilsInstallationDirectoryContent
        ),
        PageSetting(
            name: translations.settingsUtilsBugReportName,
            description: translations.settingsUtilsBugReportSubtitle,
            content: translations.settingsUtilsBugReportContent
        ),
        PageSetting(
            name: translations.settingsUtilsResetDefaultsName,
            description: translations.settingsUtilsResetDefaultsSubtitle,
            content: translations.settingsUtilsResetDefaultsContent
        )
      ],
    )
  ];
}

class _SettingsPageState extends RebootPageState<SettingsPage> {
  final GameController _gameController  = Get.find<GameController>();
  final SettingsController _settingsController = Get.find<SettingsController>();
  final UpdateController _updateController = Get.find<UpdateController>();

  @override
  Widget? get button => null;

  @override
  List<Widget> get settings => [
    _clientSettings,
    _gameServerSettings,
    _launcherUtilities
  ];

  SettingTile get _clientSettings => SettingTile(
    title: translations.settingsClientName,
    subtitle: translations.settingsClientDescription,
    expandedContent: [
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
          title: translations.settingsClientArgsName,
          subtitle: translations.settingsClientArgsDescription,
          isChild: true,
          content: TextFormBox(
            placeholder: translations.settingsClientArgsPlaceholder,
            controller: _gameController.customLaunchArgs,
          )
      ),
    ],
  );

  SettingTile get _gameServerSettings => SettingTile(
    title: translations.settingsServerName,
    subtitle: translations.settingsServerSubtitle,
    expandedContent: [
      _createFileSetting(
          title: translations.settingsServerFileName,
          description: translations.settingsServerFileDescription,
          controller: _settingsController.gameServerDll
      ),
      SettingTile(
          title: translations.settingsServerPortName,
          subtitle: translations.settingsServerPortDescription,
          content: TextFormBox(
              placeholder:  translations.settingsServerPortName,
              controller: _settingsController.gameServerPort,
              keyboardType: TextInputType.number,
              inputFormatters: [
                FilteringTextInputFormatter.digitsOnly
              ]
          ),
          isChild: true
      ),
      SettingTile(
          title: translations.settingsServerMirrorName,
          subtitle: translations.settingsServerMirrorDescription,
          content: TextFormBox(
              placeholder:  translations.settingsServerMirrorPlaceholder,
              controller: _updateController.url,
              validator: checkUpdateUrl
          ),
          isChild: true
      ),
      SettingTile(
          title: translations.settingsServerTimerName,
          subtitle: translations.settingsServerTimerSubtitle,
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
          )),
          isChild: true
      ),
    ],
  );

  SettingTile get _launcherUtilities => SettingTile(
    title: translations.settingsUtilsName,
    subtitle: translations.settingsUtilsSubtitle,
    expandedContent: [
      SettingTile(
          title: translations.settingsUtilsLanguageName,
          subtitle: translations.settingsUtilsLanguageDescription,
          isChild: true,
          content: Obx(() => DropDownButton(
              leading: Text(_getLocaleName(_settingsController.language.value)),
              items: AppLocalizations.supportedLocales.map((locale) => MenuFlyoutItem(
                  text: Text(_getLocaleName(locale.languageCode)),
                  onPressed: () => _settingsController.language.value = locale.languageCode
              )).toList()
          ))
      ),
      SettingTile(
          title: translations.settingsUtilsThemeName,
          subtitle: translations.settingsUtilsThemeDescription,
          isChild: true,
          content: Obx(() => DropDownButton(
              leading: Text(_settingsController.themeMode.value.title),
              items: ThemeMode.values.map((themeMode) => MenuFlyoutItem(
                  text: Text(themeMode.title),
                  onPressed: () => _settingsController.themeMode.value = themeMode
              )).toList()
          ))
      ),
      SettingTile(
          title: translations.settingsUtilsInstallationDirectoryName,
          subtitle: translations.settingsUtilsInstallationDirectorySubtitle,
          isChild: true,
          content: Button(
            onPressed: () => launchUrl(installationDirectory.uri),
            child: Text(translations.settingsUtilsInstallationDirectoryContent),
          )
      ),
      SettingTile(
          title: translations.settingsUtilsBugReportName,
          subtitle: translations.settingsUtilsBugReportSubtitle,
          isChild: true,
          content: Button(
            onPressed: () => launchUrl(Uri.parse("https://github.com/Auties00/reboot_launcher/issues")),
            child: Text(translations.settingsUtilsBugReportContent),
          )
      ),
      SettingTile(
          title: translations.settingsUtilsResetDefaultsName,
          subtitle: translations.settingsUtilsResetDefaultsSubtitle,
          isChild: true,
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

  Widget _createFileSetting({required String title, required String description, required TextEditingController controller}) => SettingTile(
      title: title,
      subtitle: description,
      content: FileSelector(
          placeholder: translations.selectPathPlaceholder,
          windowTitle: translations.selectPathWindowTitle,
          controller: controller,
          validator: checkDll,
          extension: "dll",
          folder: false
      ),
      isChild: true
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