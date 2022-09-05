import 'dart:io';

import 'package:archive/archive_io.dart';
import 'package:reboot_launcher/src/util/locate_binary.dart';
import 'package:http/http.dart' as http;
import 'package:crypto/crypto.dart';
import 'package:shared_preferences/shared_preferences.dart';

const _rebootUrl =
    "https://nightly.link/UWUFN/Universal-Walking-Simulator/workflows/msbuild/master/Release.zip";

Future<DateTime?> _getLastUpdate(SharedPreferences preferences) async {
  var timeInMillis = preferences.getInt("last_update");
  return timeInMillis != null ? DateTime.fromMillisecondsSinceEpoch(timeInMillis) : null;
}

Future<File> downloadRebootDll(SharedPreferences preferences) async {
  var now = DateTime.now();
  var oldRebootDll = locateBinary("reboot.dll");
  var lastUpdate = await _getLastUpdate(preferences);
  var exists = await oldRebootDll.exists();
  if(lastUpdate != null && now.difference(lastUpdate).inHours <= 24 && exists){
    return oldRebootDll;
  }

  var response = await http.get(Uri.parse(_rebootUrl));
  var tempZip = File("${Platform.environment["Temp"]}/reboot.zip")
    ..writeAsBytesSync(response.bodyBytes);
  await extractFileToDisk(tempZip.path, binariesDirectory);
  locateBinary("Project Reboot.pdb").delete();
  var rebootDll = locateBinary("Project Reboot.dll");
  if (!(await rebootDll.exists())) {
    throw Exception("Missing reboot dll");
  }

  preferences.setInt("last_update", now.millisecondsSinceEpoch);
  if (exists && sha1.convert(await oldRebootDll.readAsBytes()) == sha1.convert(await rebootDll.readAsBytes())) {
    rebootDll.delete();
    return oldRebootDll;
  }

  await rebootDll.rename(oldRebootDll.path);
  return oldRebootDll;
}
