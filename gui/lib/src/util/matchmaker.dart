import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:math';

import 'package:reboot_common/common.dart';

const Duration _timeout = Duration(seconds: 5);

Completer<bool> pingGameServerOrTimeout(String address, Duration timeout) {
  final completer = Completer<bool>();
  final start = DateTime.now();
  _pingGameServerOrTimeout(completer, start, timeout, address);
  return completer;
}

Future<void> _pingGameServerOrTimeout(Completer<bool> completer, DateTime start, Duration timeout, String address) async {
  while (!completer.isCompleted && max(DateTime.now().millisecondsSinceEpoch - start.millisecondsSinceEpoch, 0) < timeout.inMilliseconds) {
    final result = await pingGameServer(address);
    if(result) {
      completer.complete(true);
    }else {
      await Future.delayed(_timeout);
    }
  }
  if(!completer.isCompleted) {
    completer.complete(false);
  }
}

Future<bool> pingGameServer(String address) async {
  final split = address.split(":");
  var hostname = split[0];
  if(isLocalHost(hostname)) {
    hostname = "127.0.0.1";
  }

  final port = int.parse(split.length > 1 ? split[1] : kDefaultGameServerPort);
  return await _ping(hostname, port)
      .timeout(_timeout, onTimeout: () => false);
}


Future<bool> _ping(String hostname, int port) async {
  log("[MATCHMAKER] Pinging $hostname:$port");
  RawDatagramSocket? socket;
  try {
    socket = await RawDatagramSocket.bind(InternetAddress.anyIPv4, 0);
    await for (final event in socket) {
      log("[MATCHMAKER] Event: $event");
      switch(event) {
        case RawSocketEvent.read:
          log("[MATCHMAKER] Success");
          return true;
        case RawSocketEvent.write:
          log("[MATCHMAKER] Sending data");
          final dataToSend = base64Decode("AQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABA==");
          socket.send(dataToSend, InternetAddress(hostname), port);
        case RawSocketEvent.readClosed:
        case RawSocketEvent.closed:
          return false;
      }
    }

    return false;
  }catch(error) {
    log("[MATCHMAKER] Error: $error");
    return false;
  }finally {
    socket?.close();
  }
}