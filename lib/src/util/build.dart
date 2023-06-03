import 'dart:async';
import 'dart:io';
import 'dart:isolate';

import 'package:archive/archive_io.dart';
import 'package:html/parser.dart' show parse;
import 'package:http/http.dart' as http;
import 'package:reboot_launcher/src/model/fortnite_build.dart';
import 'package:reboot_launcher/src/util/time.dart';
import 'package:reboot_launcher/src/util/version.dart' as parser;
import 'package:path/path.dart' as path;

import 'os.dart';

final Uri _manifestSourceUrl = Uri.parse(
    "https://github.com/simplyblk/Fortnitebuilds/blob/main/README.md");

Future<List<FortniteBuild>> fetchBuilds(ignored) async {
  var response = await http.get(_manifestSourceUrl);
  if (response.statusCode != 200) {
    throw Exception("Erroneous status code: ${response.statusCode}");
  }

  var document = parse(response.body);
  var elements = document.getElementsByTagName("table")
      .map((element) => element.querySelector("tbody"))
      .expand((element) => element?.getElementsByTagName("tr") ?? [])
      .toList();
  var results = <FortniteBuild>[];
  for (var tableEntry in elements) {
    var children = tableEntry.querySelectorAll("td");
    var version = parser.tryParse(children[0].text);
    if (version == null) {
      continue;
    }

    var link = children[3].firstChild?.attributes?["href"];
    if (link == null || link == "N/A") {
      continue;
    }

    results.add(FortniteBuild(version: version, link: link));
  }

  return results;
}


Future<void> downloadArchiveBuild(String archiveUrl, Directory destination, Function(double, String) onProgress, Function(double?, String?) onDecompress) async {
  var outputDir = Directory("${destination.path}\\.build");
  outputDir.createSync(recursive: true);
  try {
    destination.createSync(recursive: true);
    var fileName = archiveUrl.substring(archiveUrl.lastIndexOf("/") + 1);
    var extension = path.extension(fileName);
    var tempFile = File("${outputDir.path}\\$fileName");
    if(tempFile.existsSync()) {
      tempFile.deleteSync(recursive: true);
    }

    var client = http.Client();
    var request = http.Request("GET", Uri.parse(archiveUrl));
    request.headers['Connection'] = 'Keep-Alive';
    var response = await client.send(request);
    if (response.statusCode != 200) {
      throw Exception("Erroneous status code: ${response.statusCode}");
    }

    var startTime = DateTime.now().millisecondsSinceEpoch;
    var length = response.contentLength!;
    var received = 0;
    var sink = tempFile.openWrite();
    await response.stream.map((s) {
      received += s.length;
      var now = DateTime.now();
      var eta = startTime + (now.millisecondsSinceEpoch - startTime) * length / received - now.millisecondsSinceEpoch;
      onProgress((received / length) * 100, toETA(eta));
      return s;
    }).pipe(sink);

    var receiverPort = ReceivePort();
    var file = _CompressedFile(extension, tempFile.path, destination.path, receiverPort.sendPort);
    Isolate.spawn<_CompressedFile>(_decompress, file);
    var completer = Completer();
    receiverPort.forEach((element) {
      onDecompress(element.progress, element.eta);
      if(element.progress != null && element.progress >= 100){
        completer.complete(null);
      }
    });
    await completer.future;
    delete(outputDir);
  } catch(message) {
    throw Exception("Cannot download build: $message");
  }
}

// TODO: Progress report somehow
Future<void> _decompress(_CompressedFile file) async {
  try{
    file.sendPort.send(_FileUpdate(null, null));
    switch (file.extension.toLowerCase()) {
      case '.zip':
        var process = await Process.start(
            'tar',
            ['-xf', file.tempFile, '-C', file.destination],
            mode: ProcessStartMode.inheritStdio
        );
        await process.exitCode;
        break;
      case '.rar':
        var process = await Process.start(
            '${assetsDirectory.path}\\builds\\winrar.exe',
            ['x', file.tempFile, '*.*', file.destination],
            mode: ProcessStartMode.inheritStdio
        );
        await process.exitCode;
        break;
      default:
        break;
    }
    file.sendPort.send(_FileUpdate(100, null));
  }catch(exception){
    rethrow;
  }
}

class _CompressedFile {
  final String extension;
  final String tempFile;
  final String destination;
  final SendPort sendPort;

  _CompressedFile(this.extension, this.tempFile, this.destination, this.sendPort);
}

class _FileUpdate {
  final double? progress;
  final String? eta;

  _FileUpdate(this.progress, this.eta);
}