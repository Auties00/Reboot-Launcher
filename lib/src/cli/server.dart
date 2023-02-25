import 'dart:io';

import 'package:process_run/shell.dart';
import 'package:shelf/shelf_io.dart' as shelf_io;
import 'package:shelf_proxy/shelf_proxy.dart';

import '../model/server_type.dart';
import '../util/server.dart' as server;

Future<bool> startServer(String? host, String? port, ServerType type) async {
  stdout.writeln("Starting backend server...");
  switch(type){
    case ServerType.local:
      var result = await server.ping(host ?? "127.0.0.1", port ?? "3551");
      if(result == null){
        throw Exception("Local backend server is not running");
      }

      stdout.writeln("Detected local backend server");
      return true;
    case ServerType.embedded:
      stdout.writeln("Starting an embedded server...");
      await server.startServer();
      var result = await server.ping(host ?? "127.0.0.1", port ?? "3551");
      if(result == null){
        throw Exception("Cannot start embedded server");
      }

      return true;
    case ServerType.remote:
      if(host == null){
        throw Exception("Missing host for remote server");
      }

      if(port == null){
        throw Exception("Missing host for remote server");
      }

      stdout.writeln("Starting a reverse proxy to $host:$port");
      return await _changeReverseProxyState(host, port) != null;
  }
}

Future<HttpServer?> _changeReverseProxyState(String host, String port) async {
  host = host.trim();
  if(host.isEmpty){
    throw Exception("Missing host name");
  }

  port = port.trim();
  if(port.isEmpty){
    throw Exception("Missing port");
  }

  if(int.tryParse(port) == null){
    throw Exception("Invalid port, use only numbers");
  }

  try{
    var uri = await server.ping(host, port);
    if(uri == null){
      return null;
    }

    return await shelf_io.serve(proxyHandler(uri), "127.0.0.1", 3551);
  }catch(error){
    throw Exception("Cannot start reverse proxy");
  }
}

void kill() async {
  var shell = Shell(
      commandVerbose: false,
      commentVerbose: false,
      verbose: false
  );
  try {
    await shell.run("taskkill /f /im FortniteLauncher.exe");
    await shell.run("taskkill /f /im FortniteClient-Win64-Shipping_EAC.exe");
  }catch(_){

  }
}
