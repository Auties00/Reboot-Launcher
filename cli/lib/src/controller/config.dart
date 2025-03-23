import 'dart:convert';
import 'dart:io';

import 'package:reboot_common/common.dart';

List<FortniteVersion> readVersions() {
  final file = _versionsFile;
  if(!file.existsSync()) {
    return [];
  }

  Iterable decodedVersionsJson = jsonDecode(file.readAsStringSync());
  return decodedVersionsJson
      .map((entry) => FortniteVersion.fromJson(entry))
      .toList();
}

void writeVersion(FortniteVersion version) {
  final versions = readVersions();
  versions.add(version);
  _versionsFile.writeAsString(jsonEncode(versions.map((version) => version.toJson()).toList()));
}

File get _versionsFile => File('${installationDirectory.path}/versions.json');