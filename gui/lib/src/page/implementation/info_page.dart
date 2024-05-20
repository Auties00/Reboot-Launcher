import 'package:fluent_ui/fluent_ui.dart' hide FluentIcons;
import 'package:fluentui_system_icons/fluentui_system_icons.dart';
import 'package:flutter/material.dart';
import 'package:reboot_launcher/src/page/abstract/page.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/util/tutorial.dart';
import 'package:reboot_launcher/src/widget/setting_tile.dart';

class InfoPage extends RebootPage {
  const InfoPage({Key? key}) : super(key: key);

  @override
  RebootPageState<InfoPage> createState() => _InfoPageState();

  @override
  String get name => translations.infoName;

  @override
  String get iconAsset => "assets/images/info.png";

  @override
  bool hasButton(String? routeName) => false;

  @override
  RebootPageType get type => RebootPageType.info;
}

class _InfoPageState extends RebootPageState<InfoPage> {
  @override
  List<SettingTile> get settings => [
    _documentation,
    _discord,
    _youtubeTutorial,
    _reportBug
  ];

  SettingTile get _reportBug => SettingTile(
      icon: Icon(
          FluentIcons.bug_24_regular
      ),
      title: Text(translations.settingsUtilsBugReportName),
      subtitle: Text(translations.settingsUtilsBugReportSubtitle)       ,
      content: Button(
        onPressed: openBugReport,
        child: Text(translations.settingsUtilsBugReportContent),
      )
  );

  SettingTile get _youtubeTutorial => SettingTile(
      icon: Icon(
          FluentIcons.video_24_regular
      ),
      title: Text(translations.infoVideoName),
      subtitle: Text(translations.infoVideoDescription),
      content: Button(
          onPressed: openYoutubeTutorial,
          child: Text(translations.infoVideoContent)
      )
  );

  SettingTile get _discord => SettingTile(
      icon: Icon(
          Icons.discord_outlined
      ),
      title: Text(translations.infoDiscordName),
      subtitle: Text(translations.infoDiscordDescription),
      content: Button(
          onPressed: openDiscordServer,
          child: Text(translations.infoDiscordContent)
      )
  );

  SettingTile get _documentation => SettingTile(
      icon: Icon(
          FluentIcons.document_24_regular
      ),
      title: Text(translations.infoDocumentationName),
      subtitle: Text(translations.infoDocumentationDescription),
      content: Button(
          onPressed: openTutorials,
          child: Text(translations.infoDocumentationContent)
      )
  );

  @override
  Widget? get button => null;
}