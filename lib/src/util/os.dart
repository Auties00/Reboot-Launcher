import 'dart:io';

const int appBarSize = 2;
final RegExp _regex = RegExp(r'(?<=\(Build )(.*)(?=\))');

bool get isWin11 {
  var result = _regex.firstMatch(Platform.operatingSystemVersion)?.group(1);
  if(result == null){
    return false;
  }

  var intBuild = int.tryParse(result);
  return intBuild != null && intBuild > 22000;
}

Future<File> loadBinary(String binary, bool safe) async{
  var safeBinary = File("$safeBinariesDirectory\\$binary");
  if(await safeBinary.exists()){
    return safeBinary;
  }

  var internal = _locateInternalBinary(binary);
  if(!safe){
    return internal;
  }

  if(await internal.exists()){
    await internal.copy(safeBinary.path);
  }

  return safeBinary;
}

File _locateInternalBinary(String binary){
  return File("$internalBinariesDirectory\\$binary");
}

String get internalBinariesDirectory =>
    "${File(Platform.resolvedExecutable).parent.path}\\data\\flutter_assets\\assets\\binaries";

Directory get tempDirectory =>
    Directory("${Platform.environment["Temp"]}");

String get safeBinariesDirectory =>
    "${Platform.environment["UserProfile"]}\\.reboot_launcher";