import 'dart:io';

import 'package:win32/win32.dart';
import 'package:ffi/ffi.dart';
import 'dart:ffi';

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

Future<bool> runElevated(String executable, String args) async {
  var shellInput = calloc<SHELLEXECUTEINFO>();
  shellInput.ref.lpFile = executable.toNativeUtf16();
  shellInput.ref.lpParameters = args.toNativeUtf16();
  shellInput.ref.nShow = SW_SHOWDEFAULT;
  shellInput.ref.fMask = 0x00000040;
  shellInput.ref.lpVerb = "runas".toNativeUtf16();
  shellInput.ref.cbSize = sizeOf<SHELLEXECUTEINFO>();
  var shellResult = ShellExecuteEx(shellInput);
  return shellResult == 1;
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

File loadEmbedded(String file) {
  var safeBinary = File("$safeBinariesDirectory\\backend\\cli\\$file");
  if(safeBinary.existsSync()){
    return safeBinary;
  }

  return File("${File(Platform.resolvedExecutable).parent.path}\\data\\flutter_assets\\assets\\$file");
}