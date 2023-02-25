import 'dart:io';

import 'package:win32/win32.dart';
import 'package:ffi/ffi.dart';
import 'dart:ffi';

import 'package:path/path.dart' as path;

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
  var safeBinary = File("${safeBinariesDirectory.path}\\$binary");
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

File _locateInternalBinary(String binary) =>
    File("${internalAssetsDirectory.path}\\binaries\\$binary");

Future<void> resetWinNat() async {
  var binary = await loadBinary("winnat.bat", true);
  await runElevated(binary.path, "");
}

Future<bool> runElevated(String executable, String args) async {
  var shellInput = calloc<SHELLEXECUTEINFO>();
  shellInput.ref.lpFile = executable.toNativeUtf16();
  shellInput.ref.lpParameters = args.toNativeUtf16();
  shellInput.ref.nShow = SW_HIDE;
  shellInput.ref.fMask = ES_AWAYMODE_REQUIRED;
  shellInput.ref.lpVerb = "runas".toNativeUtf16();
  shellInput.ref.cbSize = sizeOf<SHELLEXECUTEINFO>();
  var shellResult = ShellExecuteEx(shellInput);
  return shellResult == 1;
}

Directory get internalAssetsDirectory =>
    Directory("${File(Platform.resolvedExecutable).parent.path}\\data\\flutter_assets\\assets");

Directory get tempDirectory =>
    Directory("${Platform.environment["Temp"]}");

Directory get safeBinariesDirectory =>
    Directory("${Platform.environment["UserProfile"]}\\.reboot_launcher");

Directory get embeddedBackendDirectory =>
    Directory("${safeBinariesDirectory.path}\\backend-lawin");

File loadEmbedded(String file) {
  var safeBinary = File("${embeddedBackendDirectory.path}\\$file");
  if(safeBinary.existsSync()){
    return safeBinary;
  }

  safeBinary.parent.createSync(recursive: true);
  var internal = File("${internalAssetsDirectory.path}\\$file");
  if(internal.existsSync()) {
    internal.copySync(safeBinary.path);
  }

  return safeBinary;
}

Directory loadEmbeddedDirectory(String directory) {
  var safeBinary = Directory("${embeddedBackendDirectory.path}\\$directory");
  safeBinary.parent.createSync(recursive: true);
  var internal = Directory("${internalAssetsDirectory.path}\\$directory");
  _copyFolder(internal, safeBinary);
  return safeBinary;
}

void _copyFolder(Directory dir1, Directory dir2) {
  if(!dir1.existsSync()){
    return;
  }

  if (!dir2.existsSync()) {
    dir2.createSync(recursive: true);
  }

  dir1.listSync().forEach((element) {
    var newPath = "${dir2.path}/${path.basename(element.path)}";
    if (element is File) {
      var newFile = File(newPath);
      newFile.writeAsBytesSync(element.readAsBytesSync());
    } else if (element is Directory) {
      _copyFolder(element, Directory(newPath));
    }
  });
}