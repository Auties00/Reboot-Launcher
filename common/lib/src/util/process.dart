// ignore_for_file: non_constant_identifier_names

import 'dart:async';
import 'dart:convert';
import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';
import 'dart:math';

import 'package:ffi/ffi.dart';
import 'package:reboot_common/src/model/process.dart';
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
const chunkSize = 1024;

Future<void> injectDll(int pid, String dll) async {
  final process = OpenProcess(
      0x43A,
      0,
      pid
  );

  final processAddress = GetProcAddress(
      GetModuleHandle("KERNEL32".toNativeUtf16()),
      "LoadLibraryA".toNativeUtf8()
  );

  if (processAddress == nullptr) {
    throw Exception("Cannot get process address for pid $pid");
  }

  final dllAddress = VirtualAllocEx(
      process,
      nullptr,
      dll.length + 1,
      0x3000,
      0x4
  );

  final writeMemoryResult = WriteProcessMemory(
      process,
      dllAddress,
      dll.toNativeUtf8(),
      dll.length,
      nullptr
  );

  if (writeMemoryResult != 1) {
    throw Exception("Memory write failed");
  }

  final createThreadResult = _CreateRemoteThread(
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

  final closeResult = CloseHandle(process);
  if(closeResult != 1){
    throw Exception("Cannot close handle");
  }
}

void _startProcess(_ProcessParameters params) {
  final args = params.args;
  final port = params.port;
  final concatenatedArgs = args == null ? "" : " ${args.join(" ")}";
  final command = params.window ? 'cmd.exe /k ""${params.executable.path}"$concatenatedArgs"' : '"${params.executable.path}"$concatenatedArgs';
  print(command);
  final processInfo = calloc<PROCESS_INFORMATION>();
  final lpStartupInfo = calloc<STARTUPINFO>();
  lpStartupInfo.ref.cb = sizeOf<STARTUPINFO>();
  lpStartupInfo.ref.dwFlags |= STARTF_USESTDHANDLES;
  final securityAttributes = calloc<SECURITY_ATTRIBUTES>();
  securityAttributes.ref.nLength = sizeOf<SECURITY_ATTRIBUTES>();
  securityAttributes.ref.bInheritHandle = TRUE;
  final hStdOutRead = calloc<HANDLE>();
  final hStdOutWrite = calloc<HANDLE>();
  final hStdErrRead = calloc<HANDLE>();
  final hStdErrWrite = calloc<HANDLE>();
  if (CreatePipe(hStdOutRead, hStdOutWrite, securityAttributes, 0) == 0 || CreatePipe(hStdErrRead, hStdErrWrite, securityAttributes, 0) == 0) {
    final error = GetLastError();
    port.send("Cannot create process pipe: $error");
    return;
  }
  
  if(SetHandleInformation(hStdOutRead.value, HANDLE_FLAG_INHERIT, 0) == 0 || SetHandleInformation(hStdErrRead.value, HANDLE_FLAG_INHERIT, 0) == 0) {
    final error = GetLastError();
    port.send("Cannot set process pipe information: $error");
    return;
  }

  lpStartupInfo.ref.hStdOutput = hStdOutWrite.value;
  lpStartupInfo.ref.hStdError = hStdErrWrite.value;
  final success = CreateProcess(
      nullptr,
      TEXT(command),
      nullptr,
      nullptr,
      TRUE,
      NORMAL_PRIORITY_CLASS | (params.window ? CREATE_NEW_CONSOLE : CREATE_NO_WINDOW) | CREATE_NEW_PROCESS_GROUP,
      nullptr,
      TEXT(params.executable.parent.path),
      lpStartupInfo,
      processInfo
  );
  if (success == 0) {
    final error = GetLastError();
    port.send("Cannot start process: $error");
    return;
  }
  
  CloseHandle(processInfo.ref.hProcess);
  CloseHandle(processInfo.ref.hThread);
  CloseHandle(hStdOutWrite.value);
  CloseHandle(hStdErrWrite.value);
  final pid = processInfo.ref.dwProcessId;
  free(lpStartupInfo);
  free(processInfo);
  port.send(PrimitiveWin32Process(
      pid: pid,
      stdOutputHandle: hStdOutRead.value,
      errorOutputHandle: hStdErrRead.value
  ));
}

class _PipeReaderParams {
  final int handle;
  final SendPort port;

  _PipeReaderParams(this.handle, this.port);
}

void _pipeToStreamChannelled(_PipeReaderParams params) {
  final buf = calloc<Uint8>(chunkSize);
  while(true) {
    final bytesReadPtr = calloc<Uint32>();
    final success = ReadFile(params.handle, buf, chunkSize, bytesReadPtr, nullptr);
    if (success == FALSE) {
      break;
    }

    final bytesRead = bytesReadPtr.value;
    if (bytesRead == 0) {
      break;
    }

    final lines = utf8.decode(buf.asTypedList(bytesRead)).split('\n');
    for(final line in lines) {
      params.port.send(line);
    }
  }

  CloseHandle(params.handle);
  free(buf);
  Isolate.current.kill();
}

Stream<String> _pipeToStream(int pipeHandle) {
  final port = ReceivePort();
  Isolate.spawn(
      _pipeToStreamChannelled,
      _PipeReaderParams(pipeHandle, port.sendPort)
  );
  return port.map((event) => event as String);
}

class _ProcessParameters {
  File executable;
  List<String>? args;
  bool window;
  SendPort port;

  _ProcessParameters(this.executable, this.args, this.window, this.port);
}

Future<Win32Process> startProcess({required File executable, List<String>? args, bool output = true, bool window = false}) async {
  final completer = Completer<Win32Process>();
  final port = ReceivePort();
  port.listen((message) {
    if(message is PrimitiveWin32Process) {
      completer.complete(Win32Process(
          pid: message.pid,
          stdOutput: _pipeToStream(message.stdOutputHandle),
          errorOutput: _pipeToStream(message.errorOutputHandle)
      ));
    } else {
      completer.completeError(message);
    }
  });
  Isolate.spawn(
      _startProcess,
      _ProcessParameters(executable, args, window, port.sendPort),
      errorsAreFatal: true
  );
  return await completer.future;
}

final _NtResumeProcess = _ntdll.lookupFunction<Int32 Function(IntPtr hWnd),
    int Function(int hWnd)>('NtResumeProcess');

final _NtSuspendProcess = _ntdll.lookupFunction<Int32 Function(IntPtr hWnd),
    int Function(int hWnd)>('NtSuspendProcess');

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
  final args = [
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