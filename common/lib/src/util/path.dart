import 'dart:io';

Directory get installationDirectory =>
    File(Platform.resolvedExecutable).parent;

Directory get dllsDirectory => Directory("${installationDirectory.path}\\dlls");

Directory get assetsDirectory {
  final directory = Directory("${installationDirectory.path}\\data\\flutter_assets\\assets");
  if(directory.existsSync()) {
    return directory;
  }

  return installationDirectory;
}

Directory get settingsDirectory =>
    Directory("${installationDirectory.path}\\settings");

Directory get tempDirectory =>
    Directory(Platform.environment["Temp"]!);

Future<bool> delete(FileSystemEntity file) async {
  try {
    await file.delete(recursive: true);
    return true;
  }catch(_){
    return Future.delayed(const Duration(seconds: 5)).then((value) async {
      try {
        await file.delete(recursive: true);
        return true;
      }catch(_){
        return false;
      }
    });
  }
}