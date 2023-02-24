import 'dart:io';
import 'dart:math';

import 'package:html/parser.dart' show parse;
import 'package:http/http.dart' as http;
import 'package:process_run/shell.dart';
import 'package:reboot_launcher/src/model/fortnite_build.dart';
import 'package:reboot_launcher/src/util/time.dart';
import 'package:reboot_launcher/src/util/version.dart' as parser;
import 'package:version/version.dart';

import 'os.dart';

const _userAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/102.0.0.0 Safari/537.36";

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

Future<void> downloadArchiveBuild(String archiveUrl, String destination,
    Function(double, String) onProgress, Function() onDecompress) async {
  var uuid = Random.secure().nextInt(1000000);
  var extension = archiveUrl.substring(archiveUrl.lastIndexOf("."));
  var tempFile = File(
      "$destination\\.temp\\FortniteBuild$uuid$extension"
  );
  await tempFile.parent.create(recursive: true);
  try {
    var client = http.Client();
    var request = http.Request("GET", Uri.parse(archiveUrl));
    request.headers["User-Agent"] = _userAgent;
    var response = await client.send(request);
    if (response.statusCode != 200) {
      throw Exception("Erroneous status code: ${response.statusCode}");
    }

    var startTime = DateTime.now();
    var lastRemaining = -1;
    var length = response.contentLength!;
    var received = 0;
    var sink = tempFile.openWrite();
    var lastEta = toETA(0);
    await response.stream.map((entry) {
      received += entry.length;
      var percentage = (received / length) * 100;
      var elapsed = DateTime.now().difference(startTime).inMilliseconds;
      var newRemaining = (elapsed * length / received - elapsed).round();
      if(lastRemaining < 0 || lastRemaining - newRemaining <= -10000 || lastRemaining > newRemaining) {
        lastEta = toETA(lastRemaining = newRemaining);
      }

      onProgress(percentage, lastEta);
      return entry;
    }).pipe(sink);
    onDecompress();

    var output = Directory(destination);
    await output.create(recursive: true);
    await loadBinary("winrar.exe", true);
    var shell = Shell(
        commandVerbose: false,
        commentVerbose: false,
        workingDirectory: safeBinariesDirectory.path
    );
    await shell.run("./winrar.exe x \"${tempFile.path}\" *.* \"${output.path}\"");
  } finally {
    if (await tempFile.parent.exists()) {
      tempFile.parent.delete(recursive: true);
    }
  }
}
