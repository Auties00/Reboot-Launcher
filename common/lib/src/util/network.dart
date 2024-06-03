import 'dart:ffi';

import 'package:ffi/ffi.dart';
import 'package:win32/win32.dart';

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
    free(pTcpTable);
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