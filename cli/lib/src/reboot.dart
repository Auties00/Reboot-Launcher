import 'dart:io';

import 'package:archive/archive_io.dart';
import 'package:http/http.dart' as http;
import 'package:reboot_common/common.dart';

// TODO: Use github
const String _baseDownload = "https://cdn.discordapp.com/attachments/1095351875961901057/1110968021373169674/cobalt.dll";
const String _consoleDownload = "https://cdn.discordapp.com/attachments/1095351875961901057/1110968095033524234/console.dll";
const String _memoryFixDownload = "https://cdn.discordapp.com/attachments/1095351875961901057/1110968141556756581/memoryleak.dll";
const String _embeddedConfigDownload = "https://cdn.discordapp.com/attachments/1026121175878881290/1040679319351066644/embedded.zip";

Future<void> downloadRequiredDLLs() async {
  stdout.writeln("Downloading necessary components...");
  var consoleDll = File("${assetsDirectory.path}\\dlls\\console.dll");
  if(!consoleDll.existsSync()){
    var response = await http.get(Uri.parse(_consoleDownload));
    if(response.statusCode != 200){
      throw Exception("Cannot download console.dll");
    }

    await consoleDll.writeAsBytes(response.bodyBytes);
  }

  var craniumDll = File("${assetsDirectory.path}\\dlls\\cobalt.dll");
  if(!craniumDll.existsSync()){
    var response = await http.get(Uri.parse(_baseDownload));
    if(response.statusCode != 200){
      throw Exception("Cannot download cobalt.dll");
    }

    await craniumDll.writeAsBytes(response.bodyBytes);
  }

  var memoryFixDll = File("${assetsDirectory.path}\\dlls\\memoryleak.dll");
  if(!memoryFixDll.existsSync()){
    var response = await http.get(Uri.parse(_memoryFixDownload));
    if(response.statusCode != 200){
      throw Exception("Cannot download memoryleak.dll");
    }

    await memoryFixDll.writeAsBytes(response.bodyBytes);
  }

  if(!backendDirectory.existsSync()){
    var response = await http.get(Uri.parse(_embeddedConfigDownload));
    if(response.statusCode != 200){
      throw Exception("Cannot download embedded server config");
    }

    var tempZip = File("${tempDirectory.path}/reboot_config.zip");
    await tempZip.writeAsBytes(response.bodyBytes);
    await extractFileToDisk(tempZip.path, backendDirectory.path);
  }
}