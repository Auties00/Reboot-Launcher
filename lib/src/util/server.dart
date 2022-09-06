// ignore_for_file: use_build_context_synchronously

import 'dart:io';
import 'package:archive/archive_io.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:http/http.dart' as http;
import 'package:path/path.dart' as path;
import 'package:process_run/shell.dart';
import 'package:reboot_launcher/src/controller/warning_controller.dart';
import 'package:reboot_launcher/src/util/binary.dart';
import 'package:url_launcher/url_launcher.dart';

final serverLocation = Directory("${Platform.environment["UserProfile"]}/.reboot_launcher/lawin");
const String _serverUrl =
    "https://github.com/Lawin0129/LawinServer/archive/refs/heads/main.zip";
const String _nodeUrl =
    "https://nodejs.org/dist/v16.16.0/node-v16.16.0-x64.msi";

Future<void> downloadServer() async {
  var response = await http.get(Uri.parse(_serverUrl));
  var tempZip = File("${Platform.environment["Temp"]}/lawin.zip")
    ..writeAsBytesSync(response.bodyBytes);
  await extractFileToDisk(tempZip.path, serverLocation.parent.path);
  var result = Directory("${serverLocation.parent.path}/LawinServer-main");
  await result.rename("${serverLocation.parent.path}/${path.basename(serverLocation.path)}");
  await updateEngineConfig();
}

Future<void> updateEngineConfig() async {
  var engine = File("${serverLocation.path}/CloudStorage/DefaultEngine.ini");
  var patchedEngine = await loadBinary("DefaultEngine.ini", true);
  await engine.writeAsString(await patchedEngine.readAsString());
}

Future<File> downloadNode() async {
  var client = HttpClient();
  client.badCertificateCallback = ((cert, host, port) => true);
  var request = await client.getUrl(Uri.parse(_nodeUrl));
  var response = await request.close();
  var file = File("${Platform.environment["Temp"]}\\node.msi");
  await response.pipe(file.openWrite());
  return file;
}

Future<bool> isPortFree() async {
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

Future<Process?> startEmbedded(BuildContext context, bool running, bool needsFreePort) async {
  var releaseBat = await loadBinary("release.bat", false);
  if (running) {
    await Process.run(releaseBat.path, []);
    return null;
  }

  var free = await isPortFree();
  if (!free && needsFreePort) {
    var shouldKill = await _showAlreadyBindPortWarning(context);
    if (!shouldKill) {
      return null;
    }

    await Process.run(releaseBat.path, []);
  }

  if (!(await serverLocation.exists())) {
    await downloadServer();
  }

  var serverRunner = File("${serverLocation.path}/start.bat");
  if (!(await serverRunner.exists())) {
    _showNoRunnerError(context, serverRunner);
    return null;
  }

  var nodeProcess = await Process.run("where", ["node"]);
  if(nodeProcess.exitCode != 0) {
    var shouldInstall = await _showMissingNodeWarning(context);
    if (!shouldInstall) {
      return null;
    }

    var result = await _showNodeInfo(context);
    if(result == null){
      showSnackbar(
          context,
          const Snackbar(
              content: Text(
                  "Node download cancelled"
              )
          )
      );

      return null;
    }

    var controller = Get.find<WarningController>();
    controller.warning(true);
    await launchUrl(result.uri);
    return null;
  }

  var nodeModules = Directory("${serverLocation.path}/node_modules");
  if (!(await nodeModules.exists())) {
    await Process.run("${serverLocation.path}/install_packages.bat", [],
        workingDirectory: serverLocation.path);
  }

  return await Process.start(serverRunner.path, [],
      workingDirectory: serverLocation.path);
}

Future<File?> _showNodeInfo(BuildContext context) async {
  var nodeFuture = downloadNode();
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
                  label: "Downloading node installer...",
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

  return result == null || !result ? null : await nodeFuture;
}

void _showNoRunnerError(BuildContext context, File serverRunner) {
  showDialog(
      context: context,
      builder: (context) => ContentDialog(
        content: Text(
            "Cannot start server, missing start.bat at ${serverRunner.path}",
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

Future<bool> _showMissingNodeWarning(BuildContext context) async {
  return await showDialog<bool>(
      context: context,
      builder: (context) => ContentDialog(
        content: const SizedBox(
            width: double.infinity,
            child: Text("Node is required to run the embedded server",
                textAlign: TextAlign.center)),
        actions: [
          FilledButton(
            onPressed: () => Navigator.of(context).pop(false),
            style: ButtonStyle(
                backgroundColor: ButtonState.all(Colors.red)),
            child: const Text('Close'),
          ),
          FilledButton(
              child: const Text('Install'),
              onPressed: () => Navigator.of(context).pop(true)),
        ],
      )) ??
      false;
}

Future<bool> _showAlreadyBindPortWarning(BuildContext context) async {
  return await showDialog<bool>(
      context: context,
      builder: (context) => ContentDialog(
        content: const Text(
            "Port 3551 is already in use, do you want to kill the associated process?",
            textAlign: TextAlign.center),
        actions: [
          FilledButton(
            onPressed: () => Navigator.of(context).pop(false),
            style: ButtonStyle(
                backgroundColor: ButtonState.all(Colors.red)),
            child: const Text('Close'),
          ),
          FilledButton(
              child: const Text('Kill'),
              onPressed: () => Navigator.of(context).pop(true)),
        ],
      )) ??
      false;
}
