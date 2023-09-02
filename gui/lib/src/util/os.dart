import 'dart:io';

final RegExp _winBuildRegex = RegExp(r'(?<=\(Build )(.*)(?=\))');

bool get isWin11 {
  var result = _winBuildRegex.firstMatch(Platform.operatingSystemVersion)?.group(1);
  if(result == null){
    return false;
  }

  var intBuild = int.tryParse(result);
  return intBuild != null && intBuild > 22000;
}