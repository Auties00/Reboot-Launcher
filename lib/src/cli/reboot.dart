import 'dart:io';

import 'package:archive/archive_io.dart';

import '../util/os.dart';
import 'package:http/http.dart' as http;

const String _baseDownload = "https://cdn.discordapp.com/attachments/1009257632315494520/1051137082766131250/Cranium.dll";
const String _consoleDownload = "https://cdn.discordapp.com/attachments/1026121175878881290/1031230848005046373/console.dll";
const String _memoryFixDownload = "https://cdn.discordapp.com/attachments/1013220721494863872/1033484506633617500/MemoryLeakFixer.dll";
const String _embeddedConfigDownload = "https://cdn.discordapp.com/attachments/1026121175878881290/1040679319351066644/embedded.zip";

Future<void> downloadRequiredDLLs() async {
  stdout.writeln("Downloading necessary components...");
  var consoleDll = await loadBinary("console.dll", true);
  if(!consoleDll.existsSync()){
    var response = await http.get(Uri.parse(_consoleDownload));
    if(response.statusCode != 200){
      throw Exception("Cannot download console.dll");
    }

    await consoleDll.writeAsBytes(response.bodyBytes);
  }

  var craniumDll = await loadBinary("craniumv2.dll", true);
  if(!craniumDll.existsSync()){
    var response = await http.get(Uri.parse(_baseDownload));
    if(response.statusCode != 200){
      throw Exception("Cannot download craniumv2.dll");
    }

    await craniumDll.writeAsBytes(response.bodyBytes);
  }

  var memoryFixDll = await loadBinary("leakv2.dll", true);
  if(!memoryFixDll.existsSync()){
    var response = await http.get(Uri.parse(_memoryFixDownload));
    if(response.statusCode != 200){
      throw Exception("Cannot download leakv2.dll");
    }

    await memoryFixDll.writeAsBytes(response.bodyBytes);
  }

  var config = loadEmbedded("config/");
  var profiles = loadEmbedded("profiles/");
  var responses = loadEmbedded("responses/");
  if(!config.existsSync() || !profiles.existsSync() || !responses.existsSync()){
    var response = await http.get(Uri.parse(_embeddedConfigDownload));
    if(response.statusCode != 200){
      throw Exception("Cannot download embedded server config");
    }

    var tempZip = File("${tempDirectory.path}/reboot_config.zip");
    await tempZip.writeAsBytes(response.bodyBytes);

    await extractFileToDisk(tempZip.path, "$safeBinariesDirectory\\backend\\cli");
  }
}