import 'dart:collection';
import 'dart:convert';
import 'dart:io';

import 'dart:ffi';

import 'package:ffi/ffi.dart';
import 'package:win32/win32.dart';

Future<Map<String, dynamic>> getControllerJson(String name) async {
  var folder = await _getWindowsPath(FOLDERID_Documents);
  if(folder == null){
    throw Exception("Missing documents folder");
  }

  var file = File("$folder/$name.gs");
  if(!file.existsSync()){
    return HashMap();
  }

  return jsonDecode(file.readAsStringSync());
}

Future<String?> _getWindowsPath(String folderID) {
  final Pointer<Pointer<Utf16>> pathPtrPtr = calloc<Pointer<Utf16>>();
  final Pointer<GUID> knownFolderID = calloc<GUID>()..ref.setGUID(folderID);

  try {
    final int hr = SHGetKnownFolderPath(
      knownFolderID,
      KF_FLAG_DEFAULT,
      NULL,
      pathPtrPtr,
    );

    if (FAILED(hr)) {
      if (hr == E_INVALIDARG || hr == E_FAIL) {
        throw WindowsException(hr);
      }
      return Future<String?>.value();
    }

    final String path = pathPtrPtr.value.toDartString();
    return Future<String>.value(path);
  } finally {
    calloc.free(pathPtrPtr);
    calloc.free(knownFolderID);
  }
}