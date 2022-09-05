import 'dart:io';

Future<String> locateAndCopyBinary(String binary) async{
  var originalFile = locateBinary(binary);
  var tempFile = File("${Platform.environment["Temp"]}\\$binary");
  if(!(await tempFile.exists())){
    await originalFile.copy("${Platform.environment["Temp"]}\\$binary");
  }

  return tempFile.path;
}

File locateBinary(String binary){
  return File("$binariesDirectory\\$binary");
}

String get binariesDirectory =>
    "${File(Platform.resolvedExecutable).parent.path}\\data\\flutter_assets\\assets\\binaries";
