import 'package:fluent_ui/fluent_ui.dart' hide FluentIcons;
import 'package:fluentui_system_icons/fluentui_system_icons.dart';
import 'package:flutter_gen/gen_l10n/reboot_localizations.dart';
import 'package:flutter_localized_locales/flutter_localized_locales.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/dialog/implementation/data.dart';
import 'package:reboot_launcher/src/page/abstract/page.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/util/translations.dart';
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
  final SettingsController _settingsController = Get.find<SettingsController>();

  @override
  Widget? get button => null;

  @override
  List<Widget> get settings => [
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
    ),
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

  String _getLocaleName(String locale) {
    var result = LocaleNames.of(context)!.nameOf(locale);
    if(result != null) {
      return "${result.substring(0, 1).toUpperCase()}${result.substring(1).toLowerCase()}";
    }

    return locale;
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