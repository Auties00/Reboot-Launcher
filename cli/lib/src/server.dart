import 'dart:io';

import 'package:reboot_common/common.dart';
import 'package:reboot_common/src/util/authenticator.dart' as server;

Future<bool> startServerCli(String? host, String? port, ServerType type) async {
  stdout.writeln("Starting backend server...");
  switch(type){
    case ServerType.local:
      var result = await server.ping(host ?? kDefaultAuthenticatorHost, port ?? kDefaultAuthenticatorPort);
      if(result == null){
        throw Exception("Local backend server is not running");
      }

      stdout.writeln("Detected local backend server");
      return true;
    case ServerType.embedded:
      stdout.writeln("Starting an embedded server...");
      await server.startEmbeddedAuthenticator(false);
      var result = await server.ping(host ?? kDefaultAuthenticatorHost, port ?? kDefaultAuthenticatorPort);
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

    return await server.startRemoteAuthenticatorProxy(uri);
  }catch(error){
    throw Exception("Cannot start reverse proxy");
  }
}

void kill() async {
  try {
    await Process.run("taskkill", ["/f", "/im", "FortniteLauncher.exe"]);
    await Process.run("taskkill", ["/f", "/im", "FortniteClient-Win64-Shipping_EAC.exe"]);
  }catch(_){

  }
}
