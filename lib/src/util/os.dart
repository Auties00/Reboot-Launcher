import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';
import 'package:win32/win32.dart';


const int appBarSize = 2;
final RegExp _regex = RegExp(r'(?<=\(Build )(.*)(?=\))');

bool isLocalHost(String host) => host.trim() == "127.0.0.1" || host.trim().toLowerCase() == "localhost" || host.trim() == "0.0.0.0";

bool get isWin11 {
  var result = _regex.firstMatch(Platform.operatingSystemVersion)?.group(1);
  if(result == null){
    return false;
  }

  var intBuild = int.tryParse(result);
  return intBuild != null && intBuild > 22000;
}

int startBackgroundProcess(String executable, List<String> args) {
  var executablePath = TEXT('$executable ${args.map((entry) => '"$entry"').join(" ")}');
  var startupInfo = calloc<STARTUPINFO>();
  var processInfo = calloc<PROCESS_INFORMATION>();
  var success = CreateProcess(
      nullptr,
      executablePath,
      nullptr,
      nullptr,
      FALSE,
      CREATE_NO_WINDOW,
      nullptr,
      nullptr,
      startupInfo,
      processInfo
  );
  if (success == 0) {
    var error = GetLastError();
    throw Exception("Cannot start process: $error");
  }

  var pid = processInfo.ref.dwProcessId;
  free(startupInfo);
  free(processInfo);
  return pid;
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