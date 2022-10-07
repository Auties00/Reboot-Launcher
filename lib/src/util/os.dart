import 'dart:io';

import 'package:file_picker/file_picker.dart';
import 'package:path/path.dart' as path;

const int appBarSize = 2;
final RegExp _regex = RegExp(r'(?<=\(Build )(.*)(?=\))');

bool get isWin11 {
  var result = _regex.firstMatch(Platform.operatingSystemVersion)?.group(1);
  if(result == null){
    return false;
  }

  var intBuild = int.tryParse(result);
  return intBuild != null && intBuild > 22000;
}

Future<String?> openFolderPicker(String title) async =>
    await FilePicker.platform.getDirectoryPath(dialogTitle: title);

Future<String?> openFilePicker(String extension) async {
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

Future<List<Directory>> scanInstallations(String input) => Directory(input)
    .list(recursive: true)
    .handleError((_) {}, test: (e) => e is FileSystemException)
    .where((element) => path.basename(element.path) == "FortniteClient-Win64-Shipping.exe")
    .map((element) => findContainer(File(element.path)))
    .where((element) => element != null)
    .map((element) => element!)
    .toList();

Directory? findContainer(File file){
  var last = file.parent;
  for(var x = 0; x < 5; x++){
    var name = path.basename(last.path);
    if(name != "FortniteGame" || name == "Fortnite"){
      last = last.parent;
      continue;
    }

    return last.parent;
  }

  return null;
}