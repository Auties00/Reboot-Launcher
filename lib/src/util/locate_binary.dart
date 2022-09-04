import 'dart:io';

Future<String> locateBinary(String binary) async{
  var originalFile = File("$binariesDirectory\\$binary");
  var tempFile = File("${Platform.environment["Temp"]}\\$binary");
  if(!(await tempFile.exists())){
    await originalFile.copy("${Platform.environment["Temp"]}\\$binary");
  }

  return tempFile.path;
}

String get binariesDirectory =>
    "${File(Platform.resolvedExecutable).parent.path}\\data\\flutter_assets\\assets\\binaries";
