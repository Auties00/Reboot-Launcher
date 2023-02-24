import 'dart:ffi';

import 'package:win32/src/kernel32.dart';
import 'package:win32/win32.dart';

final _ntdll = DynamicLibrary.open('ntdll.dll');

// ignore: non_constant_identifier_names
int NtResumeProcess(int hWnd) {
  final function = _ntdll.lookupFunction<Int32 Function(IntPtr hWnd),
      int Function(int hWnd)>('NtResumeProcess');
  return function(hWnd);
}

// ignore: non_constant_identifier_names
int NtSuspendProcess(int hWnd) {
  final function = _ntdll.lookupFunction<Int32 Function(IntPtr hWnd),
      int Function(int hWnd)>('NtSuspendProcess');
  return function(hWnd);
}

bool suspend(int pid) {
  final processHandle = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid);
  final result = NtSuspendProcess(processHandle);
  CloseHandle(processHandle);
  return (result == 0) ? true : false;
}

bool resume(int pid) {
  final processHandle = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid);
  final result = NtResumeProcess(processHandle);
  CloseHandle(processHandle);
  return (result == 0) ? true : false;
}
