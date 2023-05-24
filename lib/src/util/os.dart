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

Directory get installationDirectory =>
    File(Platform.resolvedExecutable).parent;

Directory get logsDirectory =>
    Directory("${installationDirectory.path}\\logs");

Directory get assetsDirectory =>
    Directory("${installationDirectory.path}\\data\\flutter_assets\\assets");

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