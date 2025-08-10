import 'dart:async';
import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';

import 'package:ffi/ffi.dart';
import 'package:reboot_common/common.dart';
import 'package:win32/win32.dart';
import 'package:path/path.dart' as path;

bool useDefaultPath = false;

Directory get installationDirectory {
  if(useDefaultPath) {
    final dir = Directory('$_home/Reboot Launcher');
    dir.createSync(recursive: true);
    return dir;
  }else {
    return File(Platform.resolvedExecutable).parent;
  }
}


String get _home {
  if (Platform.isMacOS) {
    return Platform.environment['HOME'] ?? '.';
  } else if (Platform.isLinux) {
    return Platform.environment['HOME'] ?? '.';
  } else if (Platform.isWindows) {
    return Platform.environment['UserProfile'] ?? '.';
  }else {
    return '.';
  }
}

String? get antiVirusName {
  final pLoc = calloc<COMObject>();
  final rclsid = GUIDFromString(CLSID_WbemLocator);
  final riid = GUIDFromString(IID_IWbemLocator);
  final hr = CoCreateInstance(
    rclsid,
    nullptr,
    CLSCTX_INPROC_SERVER,
    riid,
    pLoc.cast(),
  );

  calloc.free(rclsid);
  calloc.free(riid);

  if (FAILED(hr)) {
    return null;
  }

  final locator = IWbemLocator(pLoc);

  final pSvc = calloc<COMObject>();
  final scope = 'ROOT\\SecurityCenter2'.toNativeUtf16();

  final hr2 = locator.connectServer(
      scope,
      nullptr,
      nullptr,
      nullptr,
      0,
      nullptr,
      nullptr,
      pSvc.cast()
  );

  calloc.free(scope);

  if (FAILED(hr2)) {
    return null;
  }

  final service = IWbemServices(pSvc);

  final pEnumerator = calloc<COMObject>();
  final wql = 'WQL'.toNativeUtf16();
  final query = 'SELECT * FROM AntiVirusProduct'.toNativeUtf16();

  final hr3 = service.execQuery(
    wql,
    query,
    WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
    nullptr,
    pEnumerator.cast(),
  );

  calloc.free(wql);
  calloc.free(query);

  if (FAILED(hr3)) {
    return null;
  }

  final enumerator = IEnumWbemClassObject(pEnumerator);

  final uReturn = calloc<Uint32>();
  final pClsObj = calloc<COMObject>();

  final hr4 = enumerator.next(
    WBEM_INFINITE,
    1,
    pClsObj.cast(),
    uReturn,
  );

  String? result;
  if (SUCCEEDED(hr4) && uReturn.value > 0) {
    final clsObj = IWbemClassObject(pClsObj);

    final vtProp = calloc<VARIANT>();
    final propName = 'displayName'.toNativeUtf16();

    final hr5 = clsObj.get(
      propName,
      0,
      vtProp,
      nullptr,
      nullptr,
    );

    calloc.free(propName);

    if (SUCCEEDED(hr5) && vtProp.ref.vt == VT_BSTR) {
      final bstr = vtProp.ref.bstrVal;
      result = bstr.toDartString();
    }

    calloc.free(vtProp);
  }

  calloc.free(uReturn);

  return result;
}

String get defaultAntiVirusName => "Windows Defender";

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

const _AF_INET = 2;
const _TCP_TABLE_OWNER_PID_LISTENER = 3;

final _getExtendedTcpTable = DynamicLibrary.open('iphlpapi.dll').lookupFunction<
    Int32 Function(Pointer, Pointer<Uint32>, Int32, Int32, Int32, Int32),
    int Function(Pointer, Pointer<Uint32>, int, int, int, int)>('GetExtendedTcpTable');

final class _MIB_TCPROW_OWNER_PID extends Struct {
  @Uint32()
  external int dwState;

  @Uint32()
  external int dwLocalAddr;

  @Uint32()
  external int dwLocalPort;

  @Uint32()
  external int dwRemoteAddr;

  @Uint32()
  external int dwRemotePort;

  @Uint32()
  external int dwOwningPid;
}

final class _MIB_TCPTABLE_OWNER_PID extends Struct {
  @Uint32()
  external int dwNumEntries;

  @Array(512)
  external Array<_MIB_TCPROW_OWNER_PID> table;
}


bool isLocalHost(String host) => host.trim() == "127.0.0.1"
    || host.trim().toLowerCase() == "localhost"
    || host.trim() == "0.0.0.0";

bool killProcessByPort(int port) {
  var pTcpTable = calloc<_MIB_TCPTABLE_OWNER_PID>();
  final dwSize = calloc<DWORD>();
  dwSize.value = 0;
  int result = _getExtendedTcpTable(
      nullptr,
      dwSize,
      FALSE,
      _AF_INET,
      _TCP_TABLE_OWNER_PID_LISTENER,
      0
  );
  if (result == ERROR_INSUFFICIENT_BUFFER) {
    calloc.free(pTcpTable);
    pTcpTable = calloc<_MIB_TCPTABLE_OWNER_PID>(dwSize.value);
    result = _getExtendedTcpTable(
        pTcpTable,
        dwSize,
        FALSE,
        _AF_INET,
        _TCP_TABLE_OWNER_PID_LISTENER,
        0
    );
  }

  if (result == NO_ERROR) {
    final table = pTcpTable.ref;
    for (int i = 0; i < table.dwNumEntries; i++) {
      final row = table.table[i];
      final localPort = _htons(row.dwLocalPort);
      if (localPort == port) {
        final pid = row.dwOwningPid;
        calloc.free(pTcpTable);
        calloc.free(dwSize);
        final hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (hProcess != NULL) {
          final result = TerminateProcess(hProcess, 0);
          CloseHandle(hProcess);
          return result != 0;
        }
        return false;
      }
    }
  }

  calloc.free(pTcpTable);
  calloc.free(dwSize);
  return false;
}

int _htons(int port) => ((port & 0xFF) << 8) | ((port >> 8) & 0xFF);

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

Future<void> injectDll(int pid, File file) async {
  try {
    await file.readAsBytes();
  }catch(_) {
    throw "${path.basename(file.path)} is not accessible";
  }

  final process = OpenProcess(0x43A, FALSE, pid);

  final processAddress = GetProcAddress(
      GetModuleHandle("KERNEL32".toNativeUtf16()),
      "LoadLibraryA".toNativeUtf8()
  );

  if (processAddress == nullptr) {
    throw "Cannot get process address for pid $pid";
  }

  final dllAddress = VirtualAllocEx(
      process,
      nullptr,
      file.path.length + 1,
      0x3000,
      0x4
  );
  if(dllAddress == 0) {
    throw "Cannot allocate memory for dll";
  }

  final writeMemoryResult = WriteProcessMemory(
      process,
      dllAddress,
      file.path.toNativeUtf8(),
      file.path.length,
      nullptr
  );
  if (writeMemoryResult != 1) {
    throw "Memory write failed";
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
    throw "Thread creation failed";
  }

  CloseHandle(process);
}

Future<bool> startElevatedProcess({required String executable, required String args, bool window = false}) async {
  var shellInput = calloc<SHELLEXECUTEINFO>();
  shellInput.ref.lpFile = executable.toNativeUtf16();
  shellInput.ref.lpParameters = args.toNativeUtf16();
  shellInput.ref.nShow = window ? SW_SHOWNORMAL : SW_HIDE;
  shellInput.ref.fMask = ES_AWAYMODE_REQUIRED;
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

final _ntdll = DynamicLibrary.open('ntdll.dll');
final _NtResumeProcess = _ntdll.lookupFunction<Int32 Function(IntPtr hWnd),
    int Function(int hWnd)>('NtResumeProcess');

final _NtSuspendProcess = _ntdll.lookupFunction<Int32 Function(IntPtr hWnd),
    int Function(int hWnd)>('NtSuspendProcess');

bool suspend(int pid) {
  final processHandle = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid);
  try {
    return _NtSuspendProcess(processHandle) == 0;
  } finally {
    CloseHandle(processHandle);
  }
}

bool resume(int pid) {
  final processHandle = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid);
  try {
    return _NtResumeProcess(processHandle) == 0;
  } finally {
    CloseHandle(processHandle);
  }
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

Future<List<File>> findFiles(Directory directory, String name) => Isolate.run(() => directory.list(recursive: true, followLinks: true)
    .handleError((_) {})
    .where((event) => event is File && path.basename(event.path) == name)
    .map((event) => event as File)
    .toList());