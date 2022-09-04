import 'dart:io';
import 'dart:math';
import 'package:http/http.dart' as http;

import 'package:process_run/shell.dart';
import 'package:reboot_launcher/src/util/locate_binary.dart';
import 'package:unrar_file/unrar_file.dart';

Future<Process> downloadManifestBuild(String manifestUrl, String destination,
    Function(double) onProgress) async {
  var process = await Process.start(await locateBinary("build.exe"), [manifestUrl, destination]);

  process.errLines
      .where((message) => message.contains("%"))
      .forEach((message) => onProgress(double.parse(message.split("%")[0])));

  return process;
}

Future<void> downloadArchiveBuild(String archiveUrl, String destination,
    Function(double) onProgress, Function() onRar) async {
  var tempFile = File("${Platform.environment["Temp"]}/FortniteBuild${Random.secure().nextInt(1000000)}.rar");
  try{
    var client = http.Client();
    var response = await client.send(http.Request("GET", Uri.parse(archiveUrl)));
    if(response.statusCode != 200){
      throw Exception("Erroneous status code: ${response.statusCode}");
    }

    print(archiveUrl);
    var length = response.contentLength!;
    var received = 0;
    var sink = tempFile.openWrite();
    await response.stream.map((s) {
      received += s.length;
      onProgress((received / length) * 100);
      return s;
    }).pipe(sink);
    onRar();
    UnrarFile.extract_rar(tempFile, destination);
  }finally{
    tempFile.delete();
  }
}
