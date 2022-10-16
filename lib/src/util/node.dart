import 'dart:io';

import 'package:archive/archive_io.dart';

import 'binary.dart';
import 'package:http/http.dart' as http;

const String _nodeUrl = "https://nodejs.org/dist/v18.11.0/node-v18.11.0-win-x86.zip";

File get embeddedNode =>
    File("$safeBinariesDirectory/node-v18.11.0-win-x86/node.exe");

Future<bool> hasNode() async {
  var nodeProcess = await Process.run("where", ["node"]);
  return nodeProcess.exitCode == 0;
}

Future<bool> downloadNode(ignored) async {
  var response = await http.get(Uri.parse(_nodeUrl));
  var tempZip = File("${tempDirectory.path}/nodejs.zip");
  await tempZip.writeAsBytes(response.bodyBytes);
  await extractFileToDisk(tempZip.path, safeBinariesDirectory);
  return true;
}