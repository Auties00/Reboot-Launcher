import 'dart:io';

import 'package:archive/archive_io.dart';
import 'package:crypto/crypto.dart';
import 'package:http/http.dart' as http;
import 'package:path/path.dart' as path;
import 'package:reboot_launcher/src/util/os.dart';

const String rebootDownloadUrl =
    "https://nightly.link/Milxnor/Project-Reboot-3.0/workflows/msbuild/main/Release.zip";
final File rebootDllFile = File("${assetsDirectory.path}\\dlls\\reboot.dll");

Future<int> downloadRebootDll(String url, int? lastUpdateMs) async {
    Directory? outputDir;
    try {
        var now = DateTime.now();
        var lastUpdate = await _getLastUpdate(lastUpdateMs);
        var exists = await rebootDllFile.exists();
        if (lastUpdate != null && now.difference(lastUpdate).inHours <= 24 && exists) {
            return lastUpdateMs!;
        }

        var response = await http.get(Uri.parse(rebootDownloadUrl));
        outputDir = await installationDirectory.createTemp("reboot_out");
        var tempZip = File("${outputDir.path}\\reboot.zip");
        await tempZip.writeAsBytes(response.bodyBytes);
        await extractFileToDisk(tempZip.path, outputDir.path);
        var rebootDll = File(outputDir.listSync().firstWhere((element) => path.extension(element.path) == ".dll").path);
        if (!exists || sha1.convert(await rebootDllFile.readAsBytes()) != sha1.convert(await rebootDll.readAsBytes())) {
            await rebootDllFile.writeAsBytes(await rebootDll.readAsBytes());
        }

        return now.millisecondsSinceEpoch;
    }catch(message) {
        throw Exception("Cannot download reboot.zip, invalid zip: $message");
    }finally{
        if(outputDir != null) {
            delete(outputDir);
        }
    }
}

Future<DateTime?> _getLastUpdate(int? lastUpdateMs) async {
  return lastUpdateMs != null
      ? DateTime.fromMillisecondsSinceEpoch(lastUpdateMs)
      : null;
}
