import 'dart:collection';
import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:path/path.dart' as path;
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/util/log.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/widget/info_tile.dart';

final _entries = SplayTreeMap<int, InfoTile>();

void initInfoTiles() {
  try {
    final directory = Directory("${assetsDirectory.path}\\info\\$currentLocale");
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
        _entries[index] = InfoTile(
            title: Text(questionName),
            content: Text(entry.readAsStringSync())
        );
      }
    }
  }catch(error) {
    log("[INFO] Error occurred while initializing info tiles: $error");
  }
}

List<InfoTile> get infoTiles => _entries.values.toList(growable: false);