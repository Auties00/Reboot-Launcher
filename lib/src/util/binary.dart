import 'dart:io';

Future<File> loadBinary(String binary, bool safe) async{
  var safeBinary = File("$safeBinariesDirectory/$binary");
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

String get safeBinariesDirectory =>
    "${Platform.environment["UserProfile"]}\\.reboot_launcher";