import 'dart:async';
import 'dart:collection';
import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart' hide FluentIcons;
import 'package:flutter/foundation.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/page/abstract/page.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/page/pages.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:path/path.dart' as path;
import 'package:reboot_launcher/src/widget/info_tile.dart';

class InfoPage extends RebootPage {
  static late final List<InfoTile> _infoTiles;
  static Object? initInfoTiles() {
    try {
      final directory = Directory("${assetsDirectory.path}\\info\\$currentLocale");
      final map = SplayTreeMap<int, InfoTile>();
      for(final entry in directory.listSync()) {
        if(entry is File) {
          final name = Uri.decodeQueryComponent(path.basename(entry.path));
          final splitter = name.indexOf(".");
          if(splitter == -1) {
            continue;
          }

          final index = int.tryParse(name.substring(0, splitter));
          if(index == null) {
            continue;
          }

          final questionName = Uri.decodeQueryComponent(name.substring(splitter + 2));
          map[index] = InfoTile(
              title: Text(questionName),
              content: Text(entry.readAsStringSync())
          );
        }
      }
      _infoTiles = map.values.toList(growable: false);
      return null;
    }catch(error) {
      _infoTiles = [];
      return error;
    }
  }

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
  final SettingsController _settingsController = Get.find<SettingsController>();
  RxInt _counter = RxInt(kDebugMode ? 0 : 180);
  late bool _showButton;

  @override
  void initState() {
    _showButton = _settingsController.firstRun.value;
    if(_settingsController.firstRun.value) {
      Timer.periodic(const Duration(seconds: 1), (timer) {
        if (_counter.value <= 0) {
          _settingsController.firstRun.value = false;
          timer.cancel();
        } else {
          _counter.value = _counter.value - 1;
        }
      });
    }
    super.initState();
  }

  @override
  List<Widget> get settings => InfoPage._infoTiles;

  @override
  Widget? get button {
    if(!_showButton) {
      return const SizedBox.shrink();
    }

    return Obx(() {
      final totalSecondsLeft = _counter.value;
      final minutesLeft = totalSecondsLeft ~/ 60;
      final secondsLeft = totalSecondsLeft % 60;
      return SizedBox(
          width: double.infinity,
          height: 48,
          child: Button(
            onPressed: totalSecondsLeft <= 0 ? () {
              _showButton = false;
              pageIndex.value = RebootPageType.play.index;
            } : null,
            child: Text(
                totalSecondsLeft <= 0 ? "I have read the instructions"
                    : "Read the instructions for at least ${secondsLeft == 0 ? '$minutesLeft minute${minutesLeft > 1 ? 's' : ''}' : minutesLeft == 0 ? '$secondsLeft second${secondsLeft > 1 ? 's' : ''}' : '$minutesLeft minute${minutesLeft > 1 ? 's' : ''} and $secondsLeft second${secondsLeft > 1 ? 's' : ''}'}"
            ),
          )
      );
    });
  }
}