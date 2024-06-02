import 'dart:io';
import 'dart:typed_data';

final Uint8List _originalHeadless = Uint8List.fromList([
  45, 0, 105, 0, 110, 0, 118, 0, 105, 0, 116, 0, 101, 0, 115, 0, 101, 0, 115, 0, 115, 0, 105, 0, 111, 0, 110, 0, 32, 0, 45, 0, 105, 0, 110, 0, 118, 0, 105, 0, 116, 0, 101, 0, 102, 0, 114, 0, 111, 0, 109, 0, 32, 0, 45, 0, 112, 0, 97, 0, 114, 0, 116, 0, 121, 0, 95, 0, 106, 0, 111, 0, 105, 0, 110, 0, 105, 0, 110, 0, 102, 0, 111, 0, 95, 0, 116, 0, 111, 0, 107, 0, 101, 0, 110, 0, 32, 0, 45, 0, 114, 0, 101, 0, 112, 0, 108, 0, 97, 0, 121, 0
]);

final Uint8List _patchedHeadless = Uint8List.fromList([
  45, 0, 108, 0, 111, 0, 103, 0, 32, 0, 45, 0, 110, 0, 111, 0, 115, 0, 112, 0, 108, 0, 97, 0, 115, 0, 104, 0, 32, 0, 45, 0, 110, 0, 111, 0, 115, 0, 111, 0, 117, 0, 110, 0, 100, 0, 32, 0, 45, 0, 110, 0, 117, 0, 108, 0, 108, 0, 114, 0, 104, 0, 105, 0, 32, 0, 45, 0, 117, 0, 115, 0, 101, 0, 111, 0, 108, 0, 100, 0, 105, 0, 116, 0, 101, 0, 109, 0, 99, 0, 97, 0, 114, 0, 100, 0, 115, 0, 32, 0, 32, 0, 32, 0, 32, 0, 32, 0, 32, 0, 32, 0
]);

final Uint8List _originalMatchmaking = Uint8List.fromList([
  63, 0, 69, 0, 110, 0, 99, 0, 114, 0, 121, 0, 112, 0, 116, 0, 105, 0, 111, 0, 110, 0, 84, 0, 111, 0, 107, 0, 101, 0, 110, 0, 61
]);

final Uint8List _patchedMatchmaking = Uint8List.fromList([
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
]);

Future<bool> patchHeadless(File file) async =>
    _patch(file, _originalHeadless, _patchedHeadless);

Future<bool> patchMatchmaking(File file) async =>
    await _patch(file, _originalMatchmaking, _patchedMatchmaking);

Future<bool> _patch(File file, Uint8List original, Uint8List patched) async {
  try {
    if(original.length != patched.length){
      throw Exception("Cannot mutate length of binary file");
    }

    final read = await file.readAsBytes();
    final length = await file.length();
    var readOffset = 0;
    var patchOffset = -1;
    var patchCount = 0;
    while(readOffset < length){
      if(read[readOffset] == original[patchCount]){
        if(patchOffset == -1) {
          patchOffset = readOffset;
        }

        if(++patchCount == original.length) {
          break;
        }
      }else {
        patchOffset = -1;
      }

      readOffset++;
    }

    if(patchOffset == -1) {
      return false;
    }

    for(var i = 0; i < patched.length; i++) {
      read[patchOffset + i] = patched[i];
    }

    await file.writeAsBytes(read, flush: true);
    return true;
  }catch(_){
    return false;
  }
}