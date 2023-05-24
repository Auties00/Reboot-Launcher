import 'dart:io';

import 'package:archive/archive_io.dart';
import 'package:html/parser.dart' show parse;
import 'package:http/http.dart' as http;
import 'package:reboot_launcher/src/model/fortnite_build.dart';
import 'package:reboot_launcher/src/util/time.dart';
import 'package:reboot_launcher/src/util/version.dart' as parser;
import 'package:path/path.dart' as path;
import 'package:unrar_file/unrar_file.dart';

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

Future<void> downloadArchiveBuild(String archiveUrl, Directory destination, Function(double, String) onProgress, Function() onRar) async {
  var outputDir = await destination.createTemp("build");
  try {
    destination.createSync(recursive: true);
    var fileName = archiveUrl.substring(archiveUrl.lastIndexOf("/") + 1);
    var extension = path.extension(fileName);
    var tempFile = File("${outputDir.path}//$fileName");
    var startTime = DateTime.now().millisecondsSinceEpoch;
    var client = http.Client();
    var response = await client.send(
        http.Request("GET", Uri.parse(archiveUrl)));
    if (response.statusCode != 200) {
      throw Exception("Erroneous status code: ${response.statusCode}");
    }

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
    onRar();
    if(extension.toLowerCase() == ".zip"){
      await extractFileToDisk(tempFile.path, destination.path);
    }else if(extension.toLowerCase() == ".rar") {
      await UnrarFile.extract_rar(tempFile.path, destination.path);
    } else {
      throw Exception("Unknown file extension: $extension");
    }
  } catch(message) {
    throw Exception("Cannot download build: $message");
  }finally {
    outputDir.delete(recursive: true);
  }
}