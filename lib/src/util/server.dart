import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:process_run/shell.dart';
import 'package:reboot_launcher/src/util/binary.dart';
import 'package:reboot_launcher/src/util/node.dart';
import 'package:reboot_launcher/src/util/server_standalone.dart';
import 'package:shelf/shelf_io.dart' as shelf_io;
import 'package:shelf_proxy/shelf_proxy.dart';

Future<bool> checkLocalServer(BuildContext context, String host, String port, bool closeAutomatically) async {
  host = host.trim();
  if(host.isEmpty){
    showSnackbar(
        context, const Snackbar(content: Text("Missing host name")));
    return false;
  }

  port = port.trim();
  if(port.isEmpty){
    showSnackbar(
        context, const Snackbar(content: Text("Missing port", textAlign: TextAlign.center)));
    return false;
  }

  if(int.tryParse(port) == null){
    showSnackbar(
        context, const Snackbar(content: Text("Invalid port, use only numbers", textAlign: TextAlign.center)));
    return false;
  }

  return await _showCheck(context, host, port, false, closeAutomatically) != null;
}


Future<HttpServer?> changeReverseProxyState(BuildContext context, String host, String port, bool closeAutomatically, HttpServer? server) async {
  if(server != null){
    try{
      server.close(force: true);
      return null;
    }catch(error){
      _showStopProxyError(context, error);
      return server;
    }
  }

  host = host.trim();
  if(host.isEmpty){
    showSnackbar(
        context, const Snackbar(content: Text("Missing host name")));
    return null;
  }

  port = port.trim();
  if(port.isEmpty){
    showSnackbar(
        context, const Snackbar(content: Text("Missing port", textAlign: TextAlign.center)));
    return null;
  }

  if(int.tryParse(port) == null){
    showSnackbar(
        context, const Snackbar(content: Text("Invalid port, use only numbers", textAlign: TextAlign.center)));
    return null;
  }

  try{
    var uri = await _showCheck(context, host, port, true, closeAutomatically);
    if(uri == null){
      return null;
    }

    return await shelf_io.serve(proxyHandler(uri), "127.0.0.1", 3551);
  }catch(error){
    _showStartProxyError(context, error);
    return null;
  }
}

Future<Uri?> _showCheck(BuildContext context, String host, String port, bool remote, bool closeAutomatically) async {
  var future = ping(host, port);
  Uri? result;
  return await showDialog(
      context: context,
      builder: (context) => ContentDialog(
        content: FutureBuilder<Uri?>(
            future: future,
            builder: (context, snapshot) {
              if(snapshot.hasError){
                return SizedBox(
                    width: double.infinity,
                    child: Text("Cannot ping ${remote ? "remote" : "local"} server: ${snapshot.error}" , textAlign: TextAlign.center)
                );
              }

              if(snapshot.connectionState == ConnectionState.done && !snapshot.hasData){
                return SizedBox(
                    width: double.infinity,
                    child: Text(
                        "The ${remote ? "remote" : "local"} server doesn't work correctly ${remote ? "or the IP and/or the port are incorrect" : ""}",
                        textAlign: TextAlign.center
                    )
                );
              }

              result = snapshot.data;
              if(snapshot.hasData){
                if(remote || closeAutomatically) {
                  Navigator.of(context).pop(result);
                }

                return const SizedBox(
                    width: double.infinity,
                    child: Text(
                        "The server works correctly",
                        textAlign: TextAlign.center
                    )
                );
              }

              return InfoLabel(
                  label: "Pinging ${remote ? "remote" : "local"} lawin server...",
                  child: const SizedBox(
                      width: double.infinity,
                      child: ProgressBar()
                  )
              );
            }
        ),
        actions: [
          SizedBox(
              width: double.infinity,
              child: Button(
                onPressed: () => Navigator.of(context).pop(result),
                child: const Text('Close'),
              ))
        ]
      )
  );
}

void _showStartProxyError(BuildContext context, Object error) {
    showDialog(
      context: context,
      builder: (context) => ContentDialog(
        content: SizedBox(
            width: double.infinity,
            child: Text("Cannot create the reverse proxy: $error", textAlign: TextAlign.center)
        ),
        actions: [
          SizedBox(
              width: double.infinity,
              child: Button(
                onPressed: () =>  Navigator.of(context).pop(),
                child: const Text('Close'),
              )
          )
        ],
      )
  );
}

void _showStopProxyError(BuildContext context, Object error) {
  showDialog(
      context: context,
      builder: (context) => ContentDialog(
        content: SizedBox(
            width: double.infinity,
            child: Text("Cannot kill the reverse proxy: $error", textAlign: TextAlign.center)
        ),
        actions: [
          SizedBox(
              width: double.infinity,
              child: Button(
                onPressed: () =>  Navigator.of(context).pop(),
                child: const Text('Close'),
              )
          )
        ],
      )
  );
}

Future<bool> changeEmbeddedServerState(BuildContext context, bool running) async {
  if (running) {
    var releaseBat = await loadBinary("release.bat", false);
    await Process.run(releaseBat.path, []);
    return false;
  }

  var free = await isLawinPortFree();
  if (!free) {
    var shouldKill = await _showAlreadyBindPortWarning(context);
    if (!shouldKill) {
      return false;
    }

    var releaseBat = await loadBinary("release.bat", false);
    await Process.run(releaseBat.path, []);
  }

  var node = await hasNode();
  var useLocalNode = false;
  if(!node) {
    useLocalNode = true;
    if(!embeddedNode.existsSync()){
      var result = await _showNodeDownloadInfo(context);
      if(!result) {
        return false;
      }
    }
  }

  if(!serverLocation.existsSync()) {
    var result = await _showServerDownloadInfo(context);
    if(!result){
      return false;
    }
  }

  var serverRunner = File("${serverLocation.path}/start.bat");
  if (!serverRunner.existsSync()) {
    _showEmbeddedError(context, "missing file ${serverRunner.path}");
    return false;
  }

  var nodeModules = Directory("${serverLocation.path}/node_modules");
  if (!nodeModules.existsSync()) {
    await Process.run("${serverLocation.path}/install_packages.bat", [],
        workingDirectory: serverLocation.path);
  }

  try {
    var logFile = await loadBinary("server.txt", true);
    if(logFile.existsSync()){
      logFile.deleteSync();
    }

    var process = await Process.start(
        !useLocalNode ? "node" : '"${embeddedNode.path}"',
        ["index.js"],
        workingDirectory: serverLocation.path
    );
    process.outLines.forEach((line) => logFile.writeAsString("$line\n", mode: FileMode.append));
    process.errLines.forEach((line) => logFile.writeAsString("$line\n", mode: FileMode.append));
    return true;
  }catch(exception){
    _showEmbeddedError(context, exception.toString());
    return false;
  }
}

Future<bool> _showServerDownloadInfo(BuildContext context) async {
  var nodeFuture = compute(downloadServer, true);
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

              return InfoLabel(
                  label: "Downloading lawin server...",
                  child: const SizedBox(
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
                  child: Button(
                    onPressed: () => Navigator.of(context).pop(snapshot.hasData && !snapshot.hasError),
                    child: Text(!snapshot.hasData && !snapshot.hasError ? 'Stop' : 'Close'),
                  )
              )
          )
        ],
      )
  );

  return result != null && result;
}

void _showEmbeddedError(BuildContext context, String error) {
  showDialog(
      context: context,
      builder: (context) => ContentDialog(
        content: SizedBox(
          width: double.infinity,
          child: Text(
              "Cannot start server: $error",
              textAlign: TextAlign.center
          )
        ),
        actions: [
          SizedBox(
              width: double.infinity,
              child: Button(
                onPressed: () => Navigator.of(context).pop(),
                child: const Text('Close'),
              ))
        ],
      ));
}

Future<bool> _showNodeDownloadInfo(BuildContext context) async {
  var nodeFuture = compute(downloadNode, true);
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

              return InfoLabel(
                  label: "Downloading node...",
                  child: const SizedBox(
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
                  child: Button(
                    onPressed: () => Navigator.of(context).pop(snapshot.hasData && !snapshot.hasError),
                    child: Text(!snapshot.hasData && !snapshot.hasError ? 'Stop' : 'Close'),
                  )
              )
          )
        ],
      )
  );

  return result != null && result;
}

Future<bool> _showAlreadyBindPortWarning(BuildContext context) async {
  return await showDialog<bool>(
      context: context,
      builder: (context) => ContentDialog(
        content: const Text(
            "Port 3551 is already in use, do you want to kill the associated process?",
            textAlign: TextAlign.center),
        actions: [
          Button(
            onPressed: () => Navigator.of(context).pop(false),
            child: const Text('Close'),
          ),
          FilledButton(
              child: const Text('Kill'),
              onPressed: () => Navigator.of(context).pop(true)),
        ],
      )) ??
      false;
}