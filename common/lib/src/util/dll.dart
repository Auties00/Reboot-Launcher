import 'dart:io';

import 'package:archive/archive_io.dart';
import 'package:http/http.dart' as http;
import 'package:path/path.dart' as path;
import 'package:reboot_common/common.dart';

bool _watcher = false;
final File rebootDllFile = File("${dllsDirectory.path}\\reboot.dll");
const String kRebootDownloadUrl =
    "http://nightly.link/Milxnor/Project-Reboot-3.0/workflows/msbuild/master/Release.zip";

Future<bool> hasRebootDllUpdate(int? lastUpdateMs, {int hours = 24, bool force = false}) async {
    final lastUpdate = await _getLastUpdate(lastUpdateMs);
    final exists = await rebootDllFile.exists();
    final now = DateTime.now();
    return force || !exists || (hours > 0 && lastUpdate != null && now.difference(lastUpdate).inHours > hours);
}

Future<void> downloadCriticalDll(String name, String outputPath) async {
    final response = await http.get(Uri.parse("https://github.com/Auties00/reboot_launcher/raw/master/gui/dependencies/dlls/$name"));
    if(response.statusCode != 200) {
        throw Exception("Cannot download $name: status code ${response.statusCode}");
    }

    final output = File(outputPath);
    await output.parent.create(recursive: true);
    await output.writeAsBytes(response.bodyBytes, flush: true);
}

Future<int> downloadRebootDll(String url) async {
    Directory? outputDir;
    final now = DateTime.now();
    try {
        final response = await http.get(Uri.parse(url));
        if(response.statusCode != 200) {
            throw Exception("Cannot download reboot.zip: status code ${response.statusCode}");
        }

        outputDir = await installationDirectory.createTemp("reboot_out");
        final tempZip = File("${outputDir.path}\\reboot.zip");
        await tempZip.writeAsBytes(response.bodyBytes, flush: true);
        await extractFileToDisk(tempZip.path, outputDir.path);
        final rebootDll = File(outputDir.listSync().firstWhere((element) => path.extension(element.path) == ".dll").path);
        await rebootDllFile.writeAsBytes(await rebootDll.readAsBytes(), flush: true);
        return now.millisecondsSinceEpoch;
    } finally{
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

Stream<String> watchDlls() async* {
    if(_watcher) {
        return;
    }

    _watcher = true;
    await for(final event in rebootDllFile.parent.watch(events: FileSystemEvent.delete | FileSystemEvent.move)) {
        if (event.path.endsWith(".dll")) {
            yield event.path;
        }
    }
}
