import 'dart:io';

import 'package:archive/archive_io.dart';
import 'package:crypto/crypto.dart';
import 'package:http/http.dart' as http;
import 'package:path/path.dart' as path;
import 'package:reboot_launcher/src/util/os.dart';

const _rebootUrl =
    "https://nightly.link/Milxnor/Universal-Walking-Simulator/workflows/msbuild/master/Release.zip";

Future<DateTime?> _getLastUpdate(int? lastUpdateMs) async {
  return lastUpdateMs != null ? DateTime.fromMillisecondsSinceEpoch(lastUpdateMs) : null;
}

Future<int> downloadRebootDll(int? lastUpdateMs) async {
  var now = DateTime.now();
  var oldRebootDll = await loadBinary("reboot.dll", true);
  var lastUpdate = await _getLastUpdate(lastUpdateMs);
  var exists = await oldRebootDll.exists();
  if(lastUpdate != null && now.difference(lastUpdate).inHours <= 24 && await oldRebootDll.exists()){
    return lastUpdateMs!;
  }

  var response = await http.get(Uri.parse(_rebootUrl));
  var tempZip = File("${tempDirectory.path}/reboot.zip");
  await tempZip.writeAsBytes(response.bodyBytes);

  var outputDir = await tempDirectory.createTemp("reboot");
  await extractFileToDisk(tempZip.path, outputDir.path);
  
  var rebootDll = outputDir.listSync()
      .firstWhere((element) => path.extension(element.path) == ".dll");
  if (exists && sha1.convert(await oldRebootDll.readAsBytes()) == sha1.convert(await File(rebootDll.path).readAsBytes())) {
    outputDir.delete(recursive: true);
    return lastUpdateMs ?? now.millisecondsSinceEpoch;
  }

  await rebootDll.rename(oldRebootDll.path);
  outputDir.delete(recursive: true);
  return now.millisecondsSinceEpoch;
}