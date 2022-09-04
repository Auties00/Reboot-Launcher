import 'package:reboot_launcher/src/model/fortnite_version.dart';
import 'package:shared_preferences/shared_preferences.dart';

class VersionController {
  final List<FortniteVersion> versions;
  final Function serializer;
  FortniteVersion? _selectedVersion;

  VersionController(
      {required this.versions,
      required this.serializer,
      FortniteVersion? selectedVersion})
      : _selectedVersion = selectedVersion;

  void add(FortniteVersion version) {
    versions.add(version);
    serializer();
  }

  FortniteVersion removeByName(String versionName) {
    var version = versions.firstWhere((element) => element.name == versionName);
    remove(version);
    return version;
  }

  void remove(FortniteVersion version) {
    versions.remove(version);
    serializer();
  }

  bool get isEmpty => versions.isEmpty;

  bool get isNotEmpty => versions.isNotEmpty;

  FortniteVersion? get selectedVersion => _selectedVersion;

  set selectedVersion(FortniteVersion? selectedVersion) {
    _selectedVersion = selectedVersion;
    SharedPreferences.getInstance().then((preferences) =>
        _selectedVersion == null
            ? preferences.remove("version")
            : preferences.setString("version", selectedVersion!.name));
  }
}
