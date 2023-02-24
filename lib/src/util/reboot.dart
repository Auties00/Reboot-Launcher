import 'dart:io';

import 'package:archive/archive_io.dart';
import 'package:crypto/crypto.dart';
import 'package:http/http.dart' as http;
import 'package:path/path.dart' as path;
import 'package:reboot_launcher/src/model/reboot_download.dart';
import 'package:reboot_launcher/src/util/os.dart';

const String rebootDownloadUrl =
    "https://nightly.link/Milxnor/Project-Reboot/workflows/msbuild/main/Release.zip";

Future<RebootDownload> downloadRebootDll(String url, int? lastUpdateMs) async {
  Directory? outputDir;
  File? tempZip;
  try {
    var now = DateTime.now();
    var oldRebootDll = await loadBinary("reboot.dll", true);
    var lastUpdate = await _getLastUpdate(lastUpdateMs);
    var exists = await oldRebootDll.exists();
    if (lastUpdate != null &&
        now.difference(lastUpdate).inHours <= 24 &&
        await oldRebootDll.exists()) {
      return RebootDownload(lastUpdateMs!);
    }

    var response = await http.get(Uri.parse(rebootDownloadUrl));
    var tempZip = await loadBinary("reboot.zip", true);
    await tempZip.writeAsBytes(response.bodyBytes);

    var outputDir = await safeBinariesDirectory.createTemp("reboot_out");
    await extractFileToDisk(tempZip.path, outputDir.path);

    var rebootDll = File(outputDir
        .listSync()
        .firstWhere((element) => path.extension(element.path) == ".dll")
        .path);

    if (!exists ||
        sha1.convert(await oldRebootDll.readAsBytes()) !=
            sha1.convert(await rebootDll.readAsBytes())) {
      await oldRebootDll.writeAsBytes(await rebootDll.readAsBytes());
    }

    return RebootDownload(now.millisecondsSinceEpoch);
  } catch (error, stackTrace) {
    return RebootDownload(-1, error, stackTrace);
  } finally {
    try {
      outputDir?.delete(recursive: true);
      tempZip?.delete();
    } catch (_) {}
  }
}

Future<DateTime?> _getLastUpdate(int? lastUpdateMs) async {
  return lastUpdateMs != null
      ? DateTime.fromMillisecondsSinceEpoch(lastUpdateMs)
      : null;
}
