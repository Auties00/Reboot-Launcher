import 'package:version/version.dart';

Version? tryParse(String version) {
  try {
    return Version.parse(version);
  } on FormatException catch (_) {
    return null;
  }
}
