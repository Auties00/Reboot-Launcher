import 'dart:io';
import 'dart:math';
import 'package:http/http.dart' as http;
import 'package:path/path.dart' as path;
import 'package:reboot_launcher/src/util/version.dart' as parser;
import 'package:html/parser.dart' show parse;

import 'package:reboot_launcher/src/model/fortnite_build.dart';

import 'package:process_run/shell.dart';
import 'package:reboot_launcher/src/util/binary.dart';

const _userAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/102.0.0.0 Safari/537.36";

final _cookieRegex = RegExp("cookie=\"(.*?);");
final _manifestSourceUrl = Uri.parse(
    "https://github.com/VastBlast/FortniteManifestArchive/blob/main/README.md");
final _archiveCookieUrl = Uri.parse("http://allinstaller.xyz/rel");
final _archiveSourceUrl = Uri.parse("http://allinstaller.xyz/rel?i=1");

Future<List<FortniteBuild>> fetchBuilds(ignored) async {
  var futures = await Future.wait([_fetchArchives(), _fetchManifests()]);
  return futures.expand((element) => element)
      .toList()
      ..sort((first, second) => first.version.compareTo(second.version));
}

Future<List<FortniteBuild>> _fetchArchives() async {
  var cookieResponse = await http.get(_archiveCookieUrl);
  var cookie = _cookieRegex.firstMatch(cookieResponse.body)?.group(1)?.trim();
  var response =
      await http.get(_archiveSourceUrl, headers: {"Cookie": cookie!});
  if (response.statusCode != 200) {
    throw Exception("Erroneous status code: ${response.statusCode}");
  }

  var document = parse(response.body);
  var results = <FortniteBuild>[];
  for (var build in document.querySelectorAll("a[href^='https']")) {
    var version = parser.tryParse(build.text.replaceAll("Build ", ""));
    if (version == null) {
      continue;
    }

    results.add(FortniteBuild(
        version: version, link: build.attributes["href"]!, hasManifest: false));
  }

  return results;
}

Future<List<FortniteBuild>> _fetchManifests() async {
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
    var separator = name.indexOf("-") + 1;
    var version = parser
        .tryParse(name.substring(separator, name.indexOf("-", separator)));
    if (version == null) {
      continue;
    }

    var link = children[2].firstChild!.attributes["href"]!;
    results.add(FortniteBuild(version: version, link: link, hasManifest: true));
  }

  return results;
}

Future<Process> downloadManifestBuild(
    String manifestUrl, String destination, Function(double) onProgress) async {
  var buildExe = await loadBinary("build.exe", false);
  var process = await Process.start(buildExe.path, [manifestUrl, destination]);

  process.errLines
      .where((message) => message.contains("%"))
      .forEach((message) => onProgress(double.parse(message.split("%")[0])));

  return process;
}

Future<void> downloadArchiveBuild(String archiveUrl, String destination,
    Function(double) onProgress, Function() onRar) async {
  var tempFile = File(
      "${Platform.environment["Temp"]}\\FortniteBuild${Random.secure().nextInt(1000000)}.rar");
  try {
    var client = http.Client();
    var request = http.Request("GET", Uri.parse(archiveUrl));
    request.headers["User-Agent"] = _userAgent;
    var response = await client.send(request);
    if (response.statusCode != 200) {
      throw Exception("Erroneous status code: ${response.statusCode}");
    }

    var length = response.contentLength!;
    var received = 0;
    var sink = tempFile.openWrite();
    await response.stream.map((s) {
      received += s.length;
      onProgress((received / length) * 100);
      return s;
    }).pipe(sink);
    onRar();

    var output = Directory(destination);
    await output.create(recursive: true);
    var shell = Shell(workingDirectory: internalBinariesDirectory);
    await shell.run("./winrar.exe x ${tempFile.path} *.* \"${output.path}\"");
  } finally {
    if (await tempFile.exists()) {
      tempFile.delete();
    }
  }
}
