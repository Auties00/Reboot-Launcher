import 'dart:io';
import 'dart:math';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:reboot_launcher/src/util/binary.dart';
import 'package:reboot_launcher/src/util/server_standalone.dart';
import 'package:shelf/shelf_io.dart' as shelf_io;
import 'package:shelf_proxy/shelf_proxy.dart';

Future<HttpServer?> changeReverseProxyState(BuildContext context, String host, String port, HttpServer? server) async {
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
    var uri = await _showReverseProxyCheck(context, host, port);
    if(uri == null){
      return null;
    }

    return await shelf_io.serve(proxyHandler(uri), "127.0.0.1", 3551);
  }catch(error){
    _showStartProxyError(context, error);
    return null;
  }
}

Future<Uri?> _showReverseProxyCheck(BuildContext context, String host, String port) async {
  var future = ping(host, port);
  return await showDialog(
      context: context,
      builder: (context) => ContentDialog(
        content: FutureBuilder<Uri?>(
            future: future,
            builder: (context, snapshot) {
              if(snapshot.hasError){
                return SizedBox(
                    width: double.infinity,
                    child: Text("Cannot ping remote server: ${snapshot.error}" , textAlign: TextAlign.center)
                );
              }

              if(snapshot.connectionState == ConnectionState.done && !snapshot.hasData){
                return const SizedBox(
                    width: double.infinity,
                    child: Text("The remote server doesn't work correctly or the IP and/or the port are incorrect" , textAlign: TextAlign.center)
                );
              }

              if(snapshot.hasData){
                Navigator.of(context).pop(snapshot.data);
              }

              return const InfoLabel(
                  label: "Pinging remote lawin server...",
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
              child: Button(
                onPressed: () => Navigator.of(context).pop(),
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
              child: Button(
                onPressed: () => Navigator.of(context).pop(),
                child: const Text('Close'),
              ))
        ],
      ));
}