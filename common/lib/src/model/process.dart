class Win32Process {
  final int pid;
  final Stream<String> stdOutput;
  final Stream<String> errorOutput;

  Win32Process({
    required this.pid,
    required this.stdOutput,
    required this.errorOutput
  });
}

class PrimitiveWin32Process {
  final int pid;
  final int stdOutputHandle;
  final int errorOutputHandle;

  PrimitiveWin32Process({
    required this.pid,
    required this.stdOutputHandle,
    required this.errorOutputHandle
  });
}