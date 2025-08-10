import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';
import 'package:file_picker/file_picker.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/scheduler.dart';
import 'package:win32/win32.dart';
import 'package:window_manager/window_manager.dart';

final RegExp _winBuildRegex = RegExp(r'(?<=\(Build )(.*)(?=\))');

int? get windowsBuild {
  final result = _winBuildRegex.firstMatch(Platform.operatingSystemVersion)?.group(1);
  if (result == null) {
    return null;
  }

  return int.tryParse(result);
}

bool get isWin11 {
  final intBuild = windowsBuild;
  return intBuild != null && intBuild > 22000;
}

Future<String?> openFolderPicker(String title) async {
  FilePicker.platform = FilePickerWindows();
  return await FilePicker.platform.getDirectoryPath(dialogTitle: title);
}

Future<String?> openFilePicker(String extension) async {
  FilePicker.platform = FilePickerWindows();
  var result = await FilePicker.platform.pickFiles(
      type: FileType.custom,
      allowMultiple: false,
      allowedExtensions: [extension]
  );
  if(result == null || result.files.isEmpty){
    return null;
  }

  return result.files.first.path;
}

bool get isDarkMode =>
    SchedulerBinding.instance.platformDispatcher.platformBrightness.isDark;

extension WindowManagerExtension on WindowManager {
  Future<void> maximizeOrRestore() async => await windowManager.isMaximized() ? windowManager.restore() : windowManager.maximize();
}

class WindowsDisk {
  static final String _nullTerminator = String.fromCharCode(0);

  final String path;
  final int freeBytesAvailable;
  final int totalNumberOfBytes;

  const WindowsDisk._internal(this.path, this.freeBytesAvailable, this.totalNumberOfBytes);

  static List<WindowsDisk> available() {
    final buffer = malloc.allocate<Utf16>(MAX_PATH);
    try {
      final length = GetLogicalDriveStrings(MAX_PATH, buffer);
      if (length == 0) {
        return [];
      }

      return buffer.toDartString(length: length)
          .split(_nullTerminator)
          .where((drive) => drive.length > 1)
          .map((driveName) {
              final freeBytesAvailable = calloc<Uint64>();
              final totalNumberOfBytes = calloc<Uint64>();
              final totalNumberOfFreeBytes = calloc<Uint64>();
              try {
                GetDiskFreeSpaceEx(
                    driveName.toNativeUtf16(),
                    freeBytesAvailable,
                    totalNumberOfBytes,
                    totalNumberOfFreeBytes
                );
                return WindowsDisk._internal(
                    driveName,
                    freeBytesAvailable.value,
                    totalNumberOfBytes.value
                );
              } finally {
                calloc.free(freeBytesAvailable);
                calloc.free(totalNumberOfBytes);
                calloc.free(totalNumberOfFreeBytes);
              }
          })
          .toList(growable: false);
    } finally {
      calloc.free(buffer);
    }
  }

  @override
  String toString() {
    return 'WindowsDisk{path: $path, freeBytesAvailable: $freeBytesAvailable, totalNumberOfBytes: $totalNumberOfBytes}';
  }
}