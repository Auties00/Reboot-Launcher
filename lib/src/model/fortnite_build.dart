import 'package:version/version.dart';

class FortniteBuild {
  final Version version;
  final String link;
  final bool hasManifest;

  FortniteBuild({required this.version, required this.link, required this.hasManifest});
}
