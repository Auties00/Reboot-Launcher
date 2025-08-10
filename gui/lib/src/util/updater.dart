import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/main.dart';
import 'package:version/version.dart';
import 'package:http/http.dart' as http;
import 'package:yaml/yaml.dart';

Future<void> checkLauncherUpdate({
  required void Function(Version) onUpdate
}) async {
  if (appVersion == null) {
    return;
  }

  final pubspec = await _getPubspecYaml();
  if (pubspec == null) {
    return;
  }

  final latestVersion = Version.parse(pubspec["version"]);
  if (latestVersion <= appVersion) {
    return;
  }

  onUpdate(latestVersion);
}

Future<dynamic> _getPubspecYaml() async {
  try {
    final pubspecResponse = await http.get(Uri.parse(
        "https://raw.githubusercontent.com/Auties00/reboot_launcher/master/gui/pubspec.yaml"));
    if (pubspecResponse.statusCode != 200) {
      return null;
    }

    return loadYaml(pubspecResponse.body);
  } catch (error) {
    log("[UPDATER] Cannot check for updates: $error");
    return null;
  }
}