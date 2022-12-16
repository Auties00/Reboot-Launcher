import 'dart:collection';
import 'dart:convert';
import 'dart:math';

import 'package:jaguar/http/context/context.dart';

const String _chars =
    'AaBbCcDdEeFfGgHhIiJjKkLlMmNnOoPpQqRrSsTtUuVvWwXxYyZz1234567890';
final Random _random = Random();

String randomString(int length) => String.fromCharCodes(
    Iterable.generate(length, (_) => _chars.codeUnitAt(_random.nextInt(_chars.length))));

double parseSeasonBuild(Context context){
  String? userAgent = context.headers.value("user-agent");
  if (userAgent == null) {
    return 1.0;
  }

  try {
    var build = userAgent.split("Release-")[1].split("-")[0];
    if (build.split(".").length == 3) {
      var value = build.split(".");
      return double.parse("${value[0]}.${value[1]}${value[2]}");
    }

    return double.parse(build);
  } catch (_) {
    return 2.0;
  }
}

int parseSeason(Context context) => int.parse(parseSeasonBuild(context).toString().split(".")[0]);

Future<HashMap<String, String?>> parseBody(Context context) async {
  var params = HashMap<String, String?>();
  utf8.decode(await context.req.body)
      .split("&")
      .map((entry) => MapEntry(entry.substring(0, entry.indexOf("=")), entry.substring(entry.indexOf("=") + 1)))
      .forEach((element) => params[element.key] = Uri.decodeQueryComponent(element.value));
  return params;
}