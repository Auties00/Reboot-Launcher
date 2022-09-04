import 'package:http/http.dart' as http;
import './../util/version.dart' as parser;
import 'package:html/parser.dart' show parse;

import '../model/fortnite_build.dart';

final _cookieRegex = RegExp("(?<=document.cookie=\")(.*)(?=\";doc)");
final _manifestSourceUrl = Uri.parse(
    "https://github.com/VastBlast/FortniteManifestArchive/blob/main/README.md");
final _archiveCookieUrl = Uri.parse("http://allinstaller.xyz/rel");
final _archiveSourceUrl = Uri.parse("http://allinstaller.xyz/rel?i=1");

Future<List<FortniteBuild>> fetchBuilds() async =>
    [...await _fetchArchives(), ...await _fetchManifests()]..sort((first, second) => first.version.compareTo(second.version));

Future<List<FortniteBuild>> _fetchArchives() async {
  var cookieResponse = await http.get(_archiveCookieUrl);
  var cookie = _cookieRegex.stringMatch(cookieResponse.body);
  var response =
      await http.get(_archiveSourceUrl, headers: {"Cookie": cookie!});
  if (response.statusCode != 200) {
    throw Exception("Erroneous status code: ${response.statusCode}");
  }

  var document = parse(response.body);
  var results = <FortniteBuild>[];
  for (var build in document.querySelectorAll("a[href^='https']")) {
    var version = parser.tryParse(build.text.replaceAll("Build ", ""));
    if(version == null){
      continue;
    }

    results.add(FortniteBuild(
        version: version,
        link: build.attributes["href"]!,
        hasManifest: false
    ));
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
    var version = parser.tryParse(name.substring(separator, name.indexOf("-", separator)));
    if(version == null){
      continue;
    }

    var link = children[2].firstChild!.attributes["href"]!;
    results.add(FortniteBuild(
        version: version,
        link: link,
        hasManifest: true
    ));
  }

  return results;
}
