// ignore_for_file: non_constant_identifier_names

import 'dart:async';
import 'dart:collection';
import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';
import 'dart:math';

import 'package:ffi/ffi.dart';
import 'package:reboot_common/common.dart';
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

Future<void> injectDll(int pid, File dll) async {
  // Get the path to the file
  final dllPath = dll.path;

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
      dllPath.length + 1,
      0x3000,
      0x4
  );

  final writeMemoryResult = WriteProcessMemory(
      process,
      dllAddress,
      dllPath.toNativeUtf8(),
      dllPath.length,
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

Future<bool> startElevatedProcess({required String executable, required String args, bool window = false}) async {
  var shellInput = calloc<SHELLEXECUTEINFO>();
  shellInput.ref.lpFile = executable.toNativeUtf16();
  shellInput.ref.lpParameters = args.toNativeUtf16();
  shellInput.ref.nShow = window ? SHOW_WINDOW_CMD.SW_SHOWNORMAL : SHOW_WINDOW_CMD.SW_HIDE;
  shellInput.ref.fMask = EXECUTION_STATE.ES_AWAYMODE_REQUIRED;
  shellInput.ref.lpVerb = "runas".toNativeUtf16();
  shellInput.ref.cbSize = sizeOf<SHELLEXECUTEINFO>();
  return ShellExecuteEx(shellInput) == 1;
}

Future<Process> startProcess({required File executable, List<String>? args, bool useTempBatch = true, bool window = false, String? name, Map<String, String>? environment}) async {
  log("[PROCESS] Starting process on ${executable.path} with $args (useTempBatch: $useTempBatch, window: $window, name: $name, environment: $environment)");
  final argsOrEmpty = args ?? [];
  final workingDirectory = _getWorkingDirectory(executable);
  if(useTempBatch) {
    final tempScriptDirectory = await tempDirectory.createTemp("reboot_launcher_process");
    final tempScriptFile = File("${tempScriptDirectory.path}\\process.bat");
    final command = window ? 'cmd.exe /k ""${executable.path}" ${argsOrEmpty.join(" ")}"' : '"${executable.path}" ${argsOrEmpty.join(" ")}';
    await tempScriptFile.writeAsString(command, flush: true);
    final process = await Process.start(
        tempScriptFile.path,
        [],
        workingDirectory: workingDirectory,
        environment: environment,
        mode: window ? ProcessStartMode.detachedWithStdio : ProcessStartMode.normal,
        runInShell: window
    );
    return _ExtendedProcess(process, true);
  }

  final process = await Process.start(
      executable.path,
      args ?? [],
      workingDirectory: workingDirectory,
      mode: window ? ProcessStartMode.detachedWithStdio : ProcessStartMode.normal,
      runInShell: window
  );
  return _ExtendedProcess(process, true);
}

String? _getWorkingDirectory(File executable) {
  try {
    log("[PROCESS] Calculating working directory for $executable");
    final workingDirectory = executable.parent.resolveSymbolicLinksSync();
    log("[PROCESS] Using working directory: $workingDirectory");
    return workingDirectory;
  }catch(error) {
    log("[PROCESS] Cannot infer working directory: $error");
    return null;
  }
}

final _NtResumeProcess = _ntdll.lookupFunction<Int32 Function(IntPtr hWnd),
    int Function(int hWnd)>('NtResumeProcess');

final _NtSuspendProcess = _ntdll.lookupFunction<Int32 Function(IntPtr hWnd),
    int Function(int hWnd)>('NtSuspendProcess');

bool suspend(int pid) {
  final processHandle = OpenProcess(PROCESS_ACCESS_RIGHTS.PROCESS_SUSPEND_RESUME, FALSE, pid);
  try {
    return _NtSuspendProcess(processHandle) == 0;
  } finally {
    CloseHandle(processHandle);
  }
}

bool resume(int pid) {
  final processHandle = OpenProcess(PROCESS_ACCESS_RIGHTS.PROCESS_SUSPEND_RESUME, FALSE, pid);
  try {
    return _NtResumeProcess(processHandle) == 0;
  } finally {
    CloseHandle(processHandle);
  }
}


Future<void> watchProcess(int pid) => Isolate.run(() {
  final processHandle = OpenProcess(FILE_ACCESS_RIGHTS.SYNCHRONIZE, FALSE, pid);
  if (processHandle == 0) {
    return;
  }

  try {
    WaitForSingleObject(processHandle, INFINITE);
  }finally {
    CloseHandle(processHandle);
  }
});

List<String> createRebootArgs(String username, String password, bool host, GameServerType hostType, bool logging, String additionalArgs) {
  log("[PROCESS] Generating reboot args");
  if(password.isEmpty) {
    username = '${_parseUsername(username, host)}@projectreboot.dev';
  }

  password = password.isNotEmpty ? password : "Rebooted";
  final args = LinkedHashMap<String, String>(
      equals: (a, b) => a.toUpperCase() == b.toUpperCase(),
      hashCode: (a) => a.toUpperCase().hashCode
  );
  args.addAll({
    "-epicapp": "Fortnite",
    "-epicenv": "Prod",
    "-epiclocale": "en-us",
    "-epicportal": "",
    "-skippatchcheck": "",
    "-nobe": "",
    "-fromfl": "eac",
    "-fltoken": "3db3ba5dcbd2e16703f3978d",
    "-caldera": "eyJhbGciOiJFUzI1NiIsInR5cCI6IkpXVCJ9.eyJhY2NvdW50X2lkIjoiYmU5ZGE1YzJmYmVhNDQwN2IyZjQwZWJhYWQ4NTlhZDQiLCJnZW5lcmF0ZWQiOjE2Mzg3MTcyNzgsImNhbGRlcmFHdWlkIjoiMzgxMGI4NjMtMmE2NS00NDU3LTliNTgtNGRhYjNiNDgyYTg2IiwiYWNQcm92aWRlciI6IkVhc3lBbnRpQ2hlYXQiLCJub3RlcyI6IiIsImZhbGxiYWNrIjpmYWxzZX0.VAWQB67RTxhiWOxx7DBjnzDnXyyEnX7OljJm-j2d88G_WgwQ9wrE6lwMEHZHjBd1ISJdUO1UVUqkfLdU5nofBQ",
    "-AUTH_LOGIN": username,
    "-AUTH_PASSWORD": password.isNotEmpty ? password : "Rebooted",
    "-AUTH_TYPE": "epic"
  });

  if(logging) {
    args["-log"] = "";
  }

  if(host) {
    args["-nosplash"] = "";
    args["-nosound"] = "";
    if(hostType == GameServerType.headless){
      args["-nullrhi"] = "";
    }
  }

  log("[PROCESS] Default args: $args");
  log("[PROCESS] Adding custom args: $additionalArgs");
  for(final additionalArg in additionalArgs.split(" ")) {
    log("[PROCESS] Processing custom arg: $additionalArg");
    final separatorIndex = additionalArg.indexOf("=");
    final argName = separatorIndex == -1 ? additionalArg : additionalArg.substring(0, separatorIndex);
    log("[PROCESS] Custom arg key: $argName");
    final argValue = separatorIndex == -1 || separatorIndex + 1 >= additionalArg.length ? "" : additionalArg.substring(separatorIndex + 1);
    log("[PROCESS] Custom arg value: $argValue");
    args[argName] = argValue;
    log("[PROCESS] Updated args: $args");
  }

  log("[PROCESS] Final args result: $args");
  return args.entries
      .map((entry) => entry.value.isEmpty ? entry.key : "${entry.key}=${entry.value}")
      .toList();
}

void handleGameOutput({
  required String line,
  required bool host,
  required void Function() onDisplayAttached,
  required void Function() onLoggedIn,
  required void Function() onMatchEnd,
  required void Function() onShutdown,
  required void Function() onTokenError,
  required void Function() onBuildCorrupted,
}) {
  if (line.contains(kShutdownLine)) {
    log("[FORTNITE_OUTPUT_HANDLER] Detected shutdown: $line");
    onShutdown();
  }else if(kCorruptedBuildErrors.any((element) => line.contains(element))){
    log("[FORTNITE_OUTPUT_HANDLER] Detected corrupt build: $line");
    onBuildCorrupted();
  }else if(kCannotConnectErrors.any((element) => line.contains(element))){
    log("[FORTNITE_OUTPUT_HANDLER] Detected cannot connect error: $line");
    onTokenError();
  }else if(kLoggedInLines.every((entry) => line.contains(entry))) {
    log("[FORTNITE_OUTPUT_HANDLER] Detected logged in: $line");
    onLoggedIn();
  }else if(line.contains(kGameFinishedLine) && host) {
    log("[FORTNITE_OUTPUT_HANDLER] Detected match end: $line");
    onMatchEnd();
  }else if(line.contains(kDisplayLine) && line.contains(kDisplayInitializedLine) && host) {
    log("[FORTNITE_OUTPUT_HANDLER] Detected display attach: $line");
    onDisplayAttached();
  }
}

String _parseUsername(String username, bool host) {
  if (username.isEmpty) {
    return kDefaultPlayerName;
  }

  username = username.replaceAll(RegExp("[^A-Za-z0-9]"), "").trim();
  if(username.isEmpty){
    return kDefaultPlayerName;
  }

  return username;
}

final class _ExtendedProcess implements Process {
  final Process _delegate;
  final Stream<List<int>>? _stdout;
  final Stream<List<int>>? _stderr;
  _ExtendedProcess(Process delegate, bool attached) :
        _delegate = delegate,
        _stdout = attached ? delegate.stdout.asBroadcastStream() : null,
        _stderr = attached ? delegate.stderr.asBroadcastStream() : null;


  @override
  Future<int> get exitCode {
    try {
      return _delegate.exitCode;
    }catch(_) {
      return watchProcess(_delegate.pid)
          .then((_) => -1);
    }
  }

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