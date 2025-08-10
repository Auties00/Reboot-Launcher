import 'dart:math';

import 'package:fluent_ui/fluent_ui.dart' hide FluentIcons;
import 'package:fluentui_system_icons/fluentui_system_icons.dart';
import 'package:flutter_localized_locales/flutter_localized_locales.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/l10n/reboot_localizations.dart';
import 'package:reboot_launcher/src/controller/dll_controller.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/pager/page_type.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/tile/file_setting_tile.dart';
import 'package:reboot_launcher/src/tile/setting_tile.dart';
import 'package:reboot_launcher/src/messenger/dialog.dart';
import 'package:reboot_launcher/src/pager/abstract_page.dart';
import 'package:url_launcher/url_launcher.dart';

final GlobalKey<TextFormBoxState> settingsConsoleDllInputKey = GlobalKey();
final GlobalKey<TextFormBoxState> settingsAuthDllInputKey = GlobalKey();
final GlobalKey<TextFormBoxState> settingsMemoryDllInputKey = GlobalKey();
final GlobalKey<TextFormBoxState> settingsGameServerDllInputKey = GlobalKey();

class SettingsPage extends AbstractPage {
  const SettingsPage({Key? key}) : super(key: key);

  @override
  String get name => translations.settingsName;

  @override
  String get iconAsset => "assets/images/settings.png";

  @override
  PageType get type => PageType.settings;

  @override
  bool hasButton(String? pageName) => false;

  @override
  AbstractPageState<SettingsPage> createState() => _SettingsPageState();
}

class _SettingsPageState extends AbstractPageState<SettingsPage> {
  final SettingsController _settingsController = Get.find<SettingsController>();
  final DllController _dllController = Get.find<DllController>();
  int? _downloadFromMirrorId;

  @override
  Widget? get button => null;

  @override
  List<Widget> get settings => [
    _language,
    _theme,
    _internalFiles,
    _installationDirectory,
  ];

  SettingTile get _internalFiles => SettingTile(
    icon: Icon(
        FluentIcons.archive_settings_24_regular
    ),
    title: Text(translations.settingsClientName),
    subtitle: Text(translations.settingsClientDescription),
    children: [
      createFileSetting(
          key: settingsConsoleDllInputKey,
          title: translations.settingsClientConsoleName,
          description: translations.settingsClientConsoleDescription,
          controller: _dllController.unrealEngineConsoleDll,
          onReset: () async {
            final path = _dllController.getDefaultDllPath(GameDll.console);
            _dllController.unrealEngineConsoleDll.text = path;
            await _dllController.download(GameDll.console, path, force: true);
            settingsConsoleDllInputKey.currentState?.validate();
          }
      ),
      createFileSetting(
          key: settingsAuthDllInputKey,
          title: translations.settingsClientAuthName,
          description: translations.settingsClientAuthDescription,
          controller: _dllController.backendDll,
          onReset: () async {
            final path = _dllController.getDefaultDllPath(GameDll.auth);
            _dllController.backendDll.text = path;
            await _dllController.download(GameDll.auth, path, force: true);
            settingsAuthDllInputKey.currentState?.validate();
          }
      ),
      createFileSetting(
          key: settingsMemoryDllInputKey,
          title: translations.settingsClientMemoryName,
          description: translations.settingsClientMemoryDescription,
          controller: _dllController.memoryLeakDll,
          onReset: () async {
            final path = _dllController.getDefaultDllPath(GameDll.memoryLeak);
            _dllController.memoryLeakDll.text = path;
            await _dllController.download(GameDll.memoryLeak, path, force: true);
            settingsAuthDllInputKey.currentState?.validate();
          }
      ),
      _gameServer
    ],
  );

  SettingTile get _gameServer => SettingTile(
    icon: Icon(
        FluentIcons.document_24_regular
    ),
    title: Text(translations.settingsServerName),
    subtitle: Text(translations.settingsServerSubtitle),
    children: [
      _internalFilesServerType,
      _internalFilesServerSource,
      _internalFilesNewServerSource,
    ],
  );

  Widget get _internalFilesServerType => SettingTile(
      icon: Icon(
          FluentIcons.games_24_regular
      ),
      title: Text(translations.settingsServerTypeName),
      subtitle: Text(translations.settingsServerTypeDescription),
      contentWidth: SettingTile.kDefaultContentWidth + 30,
      content: Obx(() => DropDownButton(
          onOpen: () => inDialog = true,
          onClose: () => inDialog = false,
          leading: Text(_dllController.customGameServer.value ? translations.settingsServerTypeCustomName : translations.settingsServerTypeEmbeddedName),
          items: {
            false: translations.settingsServerTypeEmbeddedName,
            true: translations.settingsServerTypeCustomName
          }.entries.map((entry) => MenuFlyoutItem(
              text: Text(entry.value),
              onPressed: () {
                final oldValue = _dllController.customGameServer.value;
                if(oldValue == entry.key) {
                  return;
                }

                _dllController.customGameServer.value = entry.key;
                if(!entry.key) {
                  _dllController.updateGameServerDll(
                      force: true
                  );
                }
              }
          )).toList()
      ))
  );

  Widget get _internalFilesServerSource => Obx(() {
    if(!_dllController.customGameServer.value) {
      return SettingTile(
          icon: Icon(
              FluentIcons.globe_24_regular
          ),
          title: Text(translations.settingsServerOldMirrorName),
          subtitle: Text(translations.settingsServerMirrorDescription),
          contentWidth: SettingTile.kDefaultContentWidth + 30,
          content: Row(
            children: [
              Expanded(
                child: TextFormBox(
                    placeholder:  translations.settingsServerMirrorPlaceholder,
                    controller: _dllController.beforeS20Mirror,
                    onChanged: _scheduleMirrorDownload
                ),
              ),
              const SizedBox(width: 8.0),
              Button(
                  style: ButtonStyle(
                      padding: WidgetStateProperty.all(EdgeInsets.zero)
                  ),
                  onPressed: () => _dllController.updateGameServerDll(force: true),
                  child: SizedBox.square(
                    dimension: 30,
                    child: Icon(
                        FluentIcons.arrow_download_24_regular
                    ),
                  )
              ),
              const SizedBox(width: 8.0),
              Button(
                  style: ButtonStyle(
                      padding: WidgetStateProperty.all(EdgeInsets.zero)
                  ),
                  onPressed: () {
                    _dllController.beforeS20Mirror.text = kRebootBelowS20DownloadUrl;
                    _dllController.updateGameServerDll(force: true);
                  },
                  child: SizedBox.square(
                    dimension: 30,
                    child: Icon(
                        FluentIcons.arrow_reset_24_regular
                    ),
                  )
              )
            ],
          )
      );
    }else {
      return createFileSetting(
          key: settingsGameServerDllInputKey,
          title: translations.settingsOldServerFileName,
          description: translations.settingsServerFileDescription,
          controller: _dllController.customGameServerDll,
          onReset: () async {
            final path = _dllController.getDefaultDllPath(GameDll.gameServer);
            _dllController.customGameServerDll.text = path;
            await _dllController.download(GameDll.gameServer, path);
            settingsGameServerDllInputKey.currentState?.validate();
          }
      );
    }
  });

  void _scheduleMirrorDownload(String value) async {
    if(_downloadFromMirrorId != null) {
      return;
    }

    if(Uri.tryParse(value) == null) {
      return;
    }

    final id = Random.secure().nextInt(1000000);
    _downloadFromMirrorId = id;
    await Future.delayed(const Duration(seconds: 2));
    if(_downloadFromMirrorId == id) {
      await _dllController.updateGameServerDll(force: true);
    }
    _downloadFromMirrorId = null;
  }

  Widget get _internalFilesNewServerSource => Obx(() {
    if(!_dllController.customGameServer.value) {
      return SettingTile(
          icon: Icon(
              FluentIcons.globe_24_regular
          ),
          title: Text(translations.settingsServerNewMirrorName),
          subtitle: Text(translations.settingsServerMirrorDescription),
          contentWidth: SettingTile.kDefaultContentWidth + 30,
          content: Row(
            children: [
              Expanded(
                child: TextFormBox(
                    placeholder: translations.settingsServerMirrorPlaceholder,
                    controller: _dllController.aboveS20Mirror,
                    onChanged: _scheduleMirrorDownload
                ),
              ),
              const SizedBox(width: 8.0),
              Button(
                  style: ButtonStyle(
                      padding: WidgetStateProperty.all(EdgeInsets.zero)
                  ),
                  onPressed: () => _dllController.updateGameServerDll(force: true),
                  child: SizedBox.square(
                    dimension: 30,
                    child: Icon(
                        FluentIcons.arrow_download_24_regular
                    ),
                  )
              ),
              const SizedBox(width: 8.0),
              Button(
                  style: ButtonStyle(
                      padding: WidgetStateProperty.all(EdgeInsets.zero)
                  ),
                  onPressed: () {
                    _dllController.aboveS20Mirror.text = kRebootBelowS20DownloadUrl;
                    _dllController.updateGameServerDll(force: true);
                  },
                  child: SizedBox.square(
                    dimension: 30,
                    child: Icon(
                        FluentIcons.arrow_reset_24_regular
                    ),
                  )
              )
            ],
          )
      );
    }else {
      return const SizedBox();
    }
  });

  SettingTile get _language => SettingTile(
      icon: Icon(
          FluentIcons.local_language_24_regular
      ),
      title: Text(translations.settingsUtilsLanguageName),
      subtitle: Text(translations.settingsUtilsLanguageDescription),
      content: Obx(() => DropDownButton(
          onOpen: () => inDialog = true,
          onClose: () => inDialog = false,
          leading: Text(_getLocaleName(_settingsController.language.value)),
          items: AppLocalizations.supportedLocales.map((locale) => MenuFlyoutItem(
              text: Text(_getLocaleName(locale.languageCode)),
              onPressed: () => _settingsController.language.value = locale.languageCode
          )).toList()
      ))
  );

  String _getLocaleName(String locale) {
    var result = LocaleNames.of(context)!.nameOf(locale);
    if(result != null) {
      return "${result.substring(0, 1).toUpperCase()}${result.substring(1).toLowerCase()}";
    }

    return locale;
  }

  SettingTile get _theme => SettingTile(
      icon: Icon(
          FluentIcons.dark_theme_24_regular
      ),
      title: Text(translations.settingsUtilsThemeName),
      subtitle: Text(translations.settingsUtilsThemeDescription),
      content: Obx(() => DropDownButton(
          onOpen: () => inDialog = true,
          onClose: () => inDialog = false,
          leading: Text(_settingsController.themeMode.value.title),
          items: ThemeMode.values.map((themeMode) => MenuFlyoutItem(
              text: Text(themeMode.title),
              onPressed: () => _settingsController.themeMode.value = themeMode
          )).toList()
      ))
  );

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