import 'dart:io';
import 'dart:typed_data';

import 'package:hex/hex.dart';

const String _original = "2d0069006e007600690074006500730065007300730069006f006e0020002d0069006e007600690074006500660072006f006d0020002d00700061007200740079005f006a006f0069006e0069006e0066006f005f0074006f006b0065006e0020002d007200650070006c0061007900";
const String _patched = "2d006c006f00670020002d006e006f00730070006c0061007300680020002d006e006f0073006f0075006e00640020002d006e0075006c006c0072006800690020002d007500730065006f006c0064006900740065006d00630061007200640073002000200020002000200020002000";
final Uint8List _originalBinary = Uint8List.fromList([
  45, 0, 105, 0, 110, 0, 118, 0, 105, 0, 116, 0, 101, 0, 115, 0, 101, 0, 115, 0, 115, 0, 105, 0, 111, 0, 110, 0, 32, 0, 45, 0, 105, 0, 110, 0, 118, 0, 105, 0, 116, 0, 101, 0, 102, 0, 114, 0, 111, 0, 109, 0, 32, 0, 45, 0, 112, 0, 97, 0, 114, 0, 116, 0, 121, 0, 95, 0, 106, 0, 111, 0, 105, 0, 110, 0, 105, 0, 110, 0, 102, 0, 111, 0, 95, 0, 116, 0, 111, 0, 107, 0, 101, 0, 110, 0, 32, 0, 45, 0, 114, 0, 101, 0, 112, 0, 108, 0, 97, 0, 121, 0
]);

final Uint8List _patchedBinary = Uint8List.fromList([
  45, 0, 108, 0, 111, 0, 103, 0, 32, 0, 45, 0, 110, 0, 111, 0, 115, 0, 112, 0, 108, 0, 97, 0, 115, 0, 104, 0, 32, 0, 45, 0, 110, 0, 111, 0, 115, 0, 111, 0, 117, 0, 110, 0, 100, 0, 32, 0, 45, 0, 110, 0, 117, 0, 108, 0, 108, 0, 114, 0, 104, 0, 105, 0, 32, 0, 45, 0, 117, 0, 115, 0, 101, 0, 111, 0, 108, 0, 100, 0, 105, 0, 116, 0, 101, 0, 109, 0, 99, 0, 97, 0, 114, 0, 100, 0, 115, 0, 32, 0, 32, 0, 32, 0, 32, 0, 32, 0, 32, 0, 32, 0
]);

Future<String> patchExeHex(File file) async {
  Future<String> replaceBinary(File file, String original, String replacement) async {
    var read = await file.readAsBytes();
    var hex = HEX.encode(read);
    var fixed =  hex.replaceAll(original, replacement);
    return fixed;
  }

  return await replaceBinary(file, _original, _patched);
}

Future<String> patchExeBinary(File file) async {
  Future<String> replaceBinary(File file, Uint8List original, Uint8List replacement) async {
    if(original.length != replacement.length){
      throw Exception("Cannot mutate length of binary file");
    }
    
    var read = await file.readAsBytes();
    var length = await file.length();
    var offset = 0;
    var counter = 0;
    while(offset < length){
      if(read[offset] == original[counter]){
        counter++;
      }else {
        counter = 0;
      }

      offset++;
      if(counter == original.length){
        for(var index = 0; index < replacement.length; index++){
          read[offset - counter + index] = replacement[index];
        }

        return HEX.encode(read);
      }
    }

    throw Exception("No match");
  }

  return await replaceBinary(file, _originalBinary, _patchedBinary);
}

void main() async {
  var file = File("D:\\Fortnite73\\FortniteGame\\Binaries\\Win64\\FortniteClient-Win64-Shipping.exe");
  var hexed = await patchExeHex(file);
  var binary = await patchExeBinary(file);
  var offset = 0;
  while(offset < hexed.length){
    if(hexed[offset] != binary[offset]){
      print("Difference ${hexed[offset]} != ${binary[offset]} at $offset");
    }

    offset++;
  }
  print(hexed == binary);
}