// ignore_for_file: use_build_context_synchronously

import 'dart:io';
import 'package:archive/archive_io.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:http/http.dart' as http;
import 'package:path/path.dart' as path;
import 'package:process_run/shell.dart';
import 'package:reboot_launcher/src/util/binary.dart';

final serverLocation = Directory("${Platform.environment["UserProfile"]}/.reboot_launcher/lawin");
const String _serverUrl =
    "https://github.com/Lawin0129/LawinServer/archive/refs/heads/main.zip";
const String _portableServerUrl =
    "https://cdn.discordapp.com/attachments/998020695223193673/1019999251994005504/LawinServer.exe";

Future<bool> downloadServer(bool portable) async {
  if(!portable){
    var response = await http.get(Uri.parse(_serverUrl));
    var tempZip = File("${Platform.environment["Temp"]}/lawin.zip");
    await tempZip.writeAsBytes(response.bodyBytes);
    await extractFileToDisk(tempZip.path, serverLocation.parent.path);
    var result = Directory("${serverLocation.parent.path}/LawinServer-main");
    await result.rename("${serverLocation.parent.path}/${path.basename(serverLocation.path)}");
    await Process.run("${serverLocation.path}/install_packages.bat", [], workingDirectory: serverLocation.path);
    await updateEngineConfig();
    return true;
  }

  var response = await http.get(Uri.parse(_portableServerUrl));
  var server = await loadBinary("LawinServer.exe", true);
  await server.writeAsBytes(response.bodyBytes);
  return true;
}

Future<void> updateEngineConfig() async {
  var engine = File("${serverLocation.path}/CloudStorage/DefaultEngine.ini");
  var patchedEngine = await loadBinary("DefaultEngine.ini", true);
  await engine.writeAsString(await patchedEngine.readAsString());
}

Future<bool> isLawinPortFree() async {
  var portBat = await loadBinary("port.bat", false);
  var process = await Process.run(portBat.path, []);
  return !process.outText.contains(" LISTENING "); // Goofy way, best we got
}

void checkAddress(BuildContext context, String host, String port) {
  showDialog(
      context: context,
      builder: (context) => ContentDialog(
        content: FutureBuilder<bool>(
            future: _pingAddress(host, port),
            builder: (context, snapshot) {
              if(snapshot.hasData){
                return SizedBox(
                    width: double.infinity,
                    child: Text(snapshot.data! ? "Valid address" : "Invalid address" , textAlign: TextAlign.center)
                );
              }

              return const InfoLabel(
                  label: "Checking address...",
                  child: SizedBox(
                      width: double.infinity,
                      child: ProgressBar()
                  )
              );
            }
        ),
        actions: [
          SizedBox(
              width: double.infinity,
              child: FilledButton(
                onPressed: () => Navigator.of(context).pop(),
                style: ButtonStyle(
                    backgroundColor: ButtonState.all(Colors.red)),
                child: const Text('Close'),
              ))
        ],
      )
  );
}

Future<bool> _pingAddress(String host, String port) async {
  var process = await Process.run(
      "powershell",
      ["Test-NetConnection", "-computername", host, "-port", port]
  );

  return process.exitCode == 0
      && process.outText.contains("TcpTestSucceeded : True");
}

Future<bool> changeEmbeddedServerState(BuildContext context, bool running) async {
  if (running) {
    var releaseBat = await loadBinary("release.bat", false);
    await Process.run(releaseBat.path, []);
    return false;
  }

  var nodeProcess = await Process.run("where", ["node"]);
  if(nodeProcess.exitCode == 0) {
    if(!(await serverLocation.exists()) && !(await _showServerDownloadInfo(context, false))){
      return false;
    }

    var serverRunner = File("${serverLocation.path}/start.bat");
    if (!(await serverRunner.exists())) {
      _showEmbeddedError(context, serverRunner.path);
      return false;
    }

    var nodeModules = Directory("${serverLocation.path}/node_modules");
    if (!(await nodeModules.exists())) {
      await Process.run("${serverLocation.path}/install_packages.bat", [],
          workingDirectory: serverLocation.path);
    }

    await Process.start(serverRunner.path, [],  workingDirectory: serverLocation.path);
    return true;
  }

  var portableServer = await loadBinary("LawinServer.exe", true);
  if(!(await portableServer.exists()) && !(await _showServerDownloadInfo(context, true))){
    return false;
  }

  await Process.start(portableServer.path, []);
  return true;
}

Future<bool> _showServerDownloadInfo(BuildContext context, bool portable) async {
  var nodeFuture = compute(downloadServer, portable);
  var result = await showDialog<bool>(
      context: context,
      builder: (context) => ContentDialog(
        content: FutureBuilder(
            future: nodeFuture,
            builder: (context, snapshot) {
              if(snapshot.hasError){
                return SizedBox(
                    width: double.infinity,
                    child: Text("An error occurred while downloading: ${snapshot.error}",
                        textAlign: TextAlign.center));
              }

              if(snapshot.hasData){
                return const SizedBox(
                    width: double.infinity,
                    child: Text("The download was completed successfully!",
                        textAlign: TextAlign.center)
                );
              }

              return const InfoLabel(
                  label: "Downloading lawin server...",
                  child: SizedBox(
                      width: double.infinity,
                      child: ProgressBar()
                  )
              );
            }
        ),
        actions: [
          FutureBuilder(
              future: nodeFuture,
              builder: (builder, snapshot) => SizedBox(
                  width: double.infinity,
                  child: FilledButton(
                    onPressed: () => Navigator.of(context).pop(snapshot.hasData && !snapshot.hasError),
                    style: ButtonStyle(
                        backgroundColor: ButtonState.all(Colors.red)),
                    child: Text(!snapshot.hasData && !snapshot.hasError ? 'Stop' : 'Close'),
                  )
              )
          )
        ],
      )
  );

  return result != null && result;
}

void _showEmbeddedError(BuildContext context, String path) {
  showDialog(
      context: context,
      builder: (context) => ContentDialog(
        content: Text(
            "Cannot start server, missing $path",
            textAlign: TextAlign.center),
        actions: [
          SizedBox(
              width: double.infinity,
              child: FilledButton(
                onPressed: () => Navigator.of(context).pop(),
                style: ButtonStyle(
                    backgroundColor: ButtonState.all(Colors.red)),
                child: const Text('Close'),
              ))
        ],
      ));
}