// ignore_for_file: non_constant_identifier_names

import 'dart:async';
import 'dart:convert';
import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';
import 'dart:math';
import 'package:path/path.dart' as path;

import 'package:ffi/ffi.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_common/src/extension/process.dart';
import 'package:sync/semaphore.dart';
import 'package:win32/win32.dart';

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

Future<Process> startProcess({required File executable, List<String>? args, bool wrapProcess = true, bool window = false, String? name}) async {
  final argsOrEmpty = args ?? [];
  if(wrapProcess) {
    final tempScriptDirectory = await tempDirectory.createTemp("reboot_launcher_process");
    final tempScriptFile = File("${tempScriptDirectory.path}/process.bat");
    final command = window ? 'cmd.exe /k ""${executable.path}" ${argsOrEmpty.join(" ")}"' : '"${executable.path}" ${argsOrEmpty.join(" ")}';
    await tempScriptFile.writeAsString(command, flush: true);
    final process = await Process.start(
        tempScriptFile.path,
        [],
        workingDirectory: executable.parent.path,
        mode: window ? ProcessStartMode.detachedWithStdio : ProcessStartMode.normal,
        runInShell: window
    );
    return _withLogger(name, executable, process, window);
  }

  final process = await Process.start(
      executable.path,
      args ?? [],
      workingDirectory: executable.parent.path,
      mode: window ? ProcessStartMode.detachedWithStdio : ProcessStartMode.normal,
      runInShell: window
  );
  return _withLogger(name, executable, process, window);
}

_ExtendedProcess _withLogger(String? name, File executable, Process process, bool window) {
  final extendedProcess = _ExtendedProcess(process, true);
  final loggingFile = File("${logsDirectory.path}\\${name ?? path.basenameWithoutExtension(executable.path)}-${DateTime.now().millisecondsSinceEpoch}.log");
  loggingFile.parent.createSync(recursive: true);
  if(loggingFile.existsSync()) {
    loggingFile.deleteSync();
  }

  final semaphore = Semaphore(1);
  void logEvent(String event) async {
      await semaphore.acquire();
      await loggingFile.writeAsString("$event\n", mode: FileMode.append, flush: true);
      semaphore.release();
  }
  extendedProcess.stdOutput.listen(logEvent);
  extendedProcess.stdError.listen(logEvent);
  if(!window) {
    extendedProcess.exitCode.then((value) => logEvent("Process terminated with exit code: $value\n"));
  }
  return extendedProcess;
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
  try {
    WaitForSingleObject(processHandle, INFINITE);
  }finally {
    CloseHandle(processHandle);
  }
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
  await Isolate.spawn(
      _watchProcess,
      pid,
      onExit: exitPort.sendPort,
      onError: errorPort.sendPort,
      errorsAreFatal: true
  );
  return await completer.future;
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

class _ExtendedProcess extends Process {
  final Process _delegate;
  final Stream<List<int>>? _stdout;
  final Stream<List<int>>? _stderr;
  _ExtendedProcess(Process delegate, bool attached) :
        _delegate = delegate,
        _stdout = attached ? delegate.stdout.asBroadcastStream() : null,
        _stderr = attached ? delegate.stderr.asBroadcastStream() : null;


  @override
  Future<int> get exitCode => _delegate.exitCode;

  @override
  bool kill([ProcessSignal signal = ProcessSignal.sigterm]) => _delegate.kill(signal);

  @override
  int get pid => _delegate.pid;

  @override
  IOSink get stdin => _delegate.stdin;

  @override
  Stream<List<int>> get stdout {
    final out = _stdout;
    if(out == null) {
      throw StateError("Output is not attached");
    }

    return out;
  }

  @override
  Stream<List<int>> get stderr {
    final err = _stderr;
    if(err == null) {
      throw StateError("Output is not attached");
    }

    return err;
  }
}