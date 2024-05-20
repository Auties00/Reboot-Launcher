// ignore_for_file: non_constant_identifier_names

import 'dart:async';
import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';
import 'dart:math';

import 'package:ffi/ffi.dart';
import 'package:win32/win32.dart';

import '../constant/game.dart';

final _ntdll = DynamicLibrary.open('ntdll.dll');
final _kernel32 = DynamicLibrary.open('kernel32.dll');
final _CreateRemoteThread = _kernel32.lookupFunction<
    IntPtr Function(
        IntPtr hProcess,
        Pointer<SECURITY_ATTRIBUTES> lpThreadAttributes,
        IntPtr dwStackSize,
        Pointer loadLibraryAddress,
        Pointer lpParameter,
        Uint32 dwCreationFlags,
        Pointer<Uint32> lpThreadId),
    int Function(
        int hProcess,
        Pointer<SECURITY_ATTRIBUTES> lpThreadAttributes,
        int dwStackSize,
        Pointer loadLibraryAddress,
        Pointer lpParameter,
        int dwCreationFlags,
        Pointer<Uint32> lpThreadId)>('CreateRemoteThread');

Future<void> injectDll(int pid, String dll) async {
  var process = OpenProcess(
      0x43A,
      0,
      pid
  );

  var processAddress = GetProcAddress(
      GetModuleHandle("KERNEL32".toNativeUtf16()),
      "LoadLibraryA".toNativeUtf8()
  );

  if (processAddress == nullptr) {
    throw Exception("Cannot get process address for pid $pid");
  }

  var dllAddress = VirtualAllocEx(
      process,
      nullptr,
      dll.length + 1,
      0x3000,
      0x4
  );

  var writeMemoryResult = WriteProcessMemory(
      process,
      dllAddress,
      dll.toNativeUtf8(),
      dll.length,
      nullptr
  );

  if (writeMemoryResult != 1) {
    throw Exception("Memory write failed");
  }

  var createThreadResult = _CreateRemoteThread(
      process,
      nullptr,
      0,
      processAddress,
      dllAddress,
      0,
      nullptr
  );

  if (createThreadResult == -1) {
    throw Exception("Thread creation failed");
  }

  var closeResult = CloseHandle(process);
  if(closeResult != 1){
    throw Exception("Cannot close handle");
  }
}

bool runElevatedProcess(String executable, String args) {
  final shellInput = calloc<SHELLEXECUTEINFO>();
  shellInput.ref.lpFile = executable.toNativeUtf16();
  shellInput.ref.lpParameters = args.toNativeUtf16();
  shellInput.ref.nShow = SW_HIDE;
  shellInput.ref.fMask = ES_AWAYMODE_REQUIRED;
  shellInput.ref.lpVerb = "runas".toNativeUtf16();
  shellInput.ref.cbSize = sizeOf<SHELLEXECUTEINFO>();
  final result = ShellExecuteEx(shellInput) == 1;
  free(shellInput);
  return result;
}

void _startBackgroundProcess(_BackgroundProcessParameters params) {
  var args = params.args;
  var concatenatedArgs = args == null ? "" : " ${args.map((entry) => '"$entry"').join(" ")}";
  var executablePath = TEXT('cmd.exe /k "${params.executable.path}"$concatenatedArgs');
  var startupInfo = calloc<STARTUPINFO>();
  var processInfo = calloc<PROCESS_INFORMATION>();
  var windowFlag = params.window ? CREATE_NEW_CONSOLE : CREATE_NO_WINDOW;
  var success = CreateProcess(
      nullptr,
      executablePath,
      nullptr,
      nullptr,
      FALSE,
      NORMAL_PRIORITY_CLASS | windowFlag | CREATE_NEW_PROCESS_GROUP,
      nullptr,
      TEXT(params.executable.parent.path),
      startupInfo,
      processInfo
  );
  if (success == 0) {
    var error = GetLastError();
    params.port.send("Cannot start process: $error");
    return;
  }

  var pid = processInfo.ref.dwProcessId;
  free(startupInfo);
  free(processInfo);
  params.port.send(pid);
}

class _BackgroundProcessParameters {
  File executable;
  List<String>? args;
  bool window;
  SendPort port;

  _BackgroundProcessParameters(this.executable, this.args, this.window, this.port);
}

Future<int> startBackgroundProcess({required File executable, List<String>? args, bool window = false}) async {
  var completer = Completer<int>();
  var port = ReceivePort();
  port.listen((message) => message is int ? completer.complete(message) : completer.completeError(message));
  var isolate = await Isolate.spawn(
      _startBackgroundProcess,
      _BackgroundProcessParameters(executable, args, window, port.sendPort),
      errorsAreFatal: true
  );
  var result = await completer.future;
  isolate.kill(priority: Isolate.immediate);
  return result;
}

int _NtResumeProcess(int hWnd) {
  final function = _ntdll.lookupFunction<Int32 Function(IntPtr hWnd),
      int Function(int hWnd)>('NtResumeProcess');
  return function(hWnd);
}

int _NtSuspendProcess(int hWnd) {
  final function = _ntdll.lookupFunction<Int32 Function(IntPtr hWnd),
      int Function(int hWnd)>('NtSuspendProcess');
  return function(hWnd);
}

bool suspend(int pid) {
  final processHandle = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid);
  final result = _NtSuspendProcess(processHandle);
  CloseHandle(processHandle);
  return result == 0;
}

bool resume(int pid) {
  final processHandle = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid);
  final result = _NtResumeProcess(processHandle);
  CloseHandle(processHandle);
  return result == 0;
}

void _watchProcess(int pid) {
  final processHandle = OpenProcess(SYNCHRONIZE, FALSE, pid);
  WaitForSingleObject(processHandle, INFINITE);
  CloseHandle(processHandle);
}

Future<bool> watchProcess(int pid) async {
  var completer = Completer<bool>();
  var exitPort = ReceivePort();
  exitPort.listen((_) {
    if(!completer.isCompleted) {
      completer.complete(true);
    }
  });
  var errorPort = ReceivePort();
  errorPort.listen((_) => completer.complete(false));
  var isolate = await Isolate.spawn(
      _watchProcess,
      pid,
      onExit: exitPort.sendPort,
      onError: errorPort.sendPort,
      errorsAreFatal: true
  );
  var result = await completer.future;
  isolate.kill(priority: Isolate.immediate);
  return result;
}

List<String> createRebootArgs(String username, String password, bool host, bool headless, String additionalArgs) {
  if(password.isEmpty) {
    username = '${_parseUsername(username, host)}@projectreboot.dev';
  }

  password = password.isNotEmpty ? password : "Rebooted";
  var args = [
    "-epicapp=Fortnite",
    "-epicenv=Prod",
    "-epiclocale=en-us",
    "-epicportal",
    "-skippatchcheck",
    "-nobe",
    "-fromfl=eac",
    "-fltoken=3db3ba5dcbd2e16703f3978d",
    "-caldera=eyJhbGciOiJFUzI1NiIsInR5cCI6IkpXVCJ9.eyJhY2NvdW50X2lkIjoiYmU5ZGE1YzJmYmVhNDQwN2IyZjQwZWJhYWQ4NTlhZDQiLCJnZW5lcmF0ZWQiOjE2Mzg3MTcyNzgsImNhbGRlcmFHdWlkIjoiMzgxMGI4NjMtMmE2NS00NDU3LTliNTgtNGRhYjNiNDgyYTg2IiwiYWNQcm92aWRlciI6IkVhc3lBbnRpQ2hlYXQiLCJub3RlcyI6IiIsImZhbGxiYWNrIjpmYWxzZX0.VAWQB67RTxhiWOxx7DBjnzDnXyyEnX7OljJm-j2d88G_WgwQ9wrE6lwMEHZHjBd1ISJdUO1UVUqkfLdU5nofBQ",
    "-AUTH_LOGIN=$username",
    "-AUTH_PASSWORD=${password.isNotEmpty ? password : "Rebooted"}",
    "-AUTH_TYPE=epic"
  ];

  if(host && headless){
    args.addAll([
      "-nullrhi",
      "-nosplash",
      "-nosound",
    ]);
  }

  if(additionalArgs.isNotEmpty){
    args.addAll(additionalArgs.split(" "));
  }

  return args;
}

String _parseUsername(String username, bool host) {
  if(host) {
    return "Player${Random().nextInt(1000)}";
  }

  if (username.isEmpty) {
    return kDefaultPlayerName;
  }

  username = username.replaceAll(RegExp("[^A-Za-z0-9]"), "").trim();
  if(username.isEmpty){
    return kDefaultPlayerName;
  }

  return username;
}