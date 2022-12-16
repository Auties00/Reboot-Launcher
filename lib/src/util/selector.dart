import 'package:file_picker/file_picker.dart';

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