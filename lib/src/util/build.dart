import 'dart:io';

import 'package:html/parser.dart' show parse;
import 'package:http/http.dart' as http;
import 'package:process_run/shell.dart';
import 'package:reboot_launcher/src/model/fortnite_build.dart';
import 'package:reboot_launcher/src/util/version.dart' as parser;

import 'os.dart';

final _manifestSourceUrl = Uri.parse(
    "https://github.com/VastBlast/FortniteManifestArchive/blob/main/README.md");


Future<List<FortniteBuild>> fetchBuilds(ignored) async {
  var response = await http.get(_manifestSourceUrl);
  if (response.statusCode != 200) {
    throw Exception("Erroneous status code: ${response.statusCode}");
  }

  var document = parse(response.body);
  var table = document.querySelector("table");
  if (table == null) {
    throw Exception("Missing data table");
  }

  var results = <FortniteBuild>[];
  for (var tableEntry in table.querySelectorAll("tbody > tr")) {
    var children = tableEntry.querySelectorAll("td");

    var name = children[0].text;
    var minifiedName = name.substring(name.indexOf("-") + 1, name.lastIndexOf("-"));
    var version = parser
        .tryParse(minifiedName.replaceFirst("", ""));
    if (version == null) {
      continue;
    }

    var link = children[2].firstChild!.attributes["href"]!;
    results.add(FortniteBuild(version: version, link: link));
  }

  return results;
}

Future<Process> downloadManifestBuild(
    String manifestUrl, String destination, Function(double, String) onProgress) async {
  var log = await loadBinary("download.txt", true);
  await log.create();

  var buildExe = await loadBinary("build.exe", true);
  var process = await Process.start(buildExe.path, [manifestUrl, destination]);

  log.writeAsString("Starting download of: $manifestUrl\n", mode: FileMode.append);
  process.errLines
      .where((message) => message.contains("%"))
      .forEach((message) {
    log.writeAsString("$message\n", mode: FileMode.append);
    onProgress(double.parse(message.split("%")[0]), message.substring(message.indexOf(" ") + 1));
  });

  return process;
}
