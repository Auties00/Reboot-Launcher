import 'dart:convert';
import 'dart:io';

import 'package:reboot_common/common.dart';

const Duration _timeout = Duration(seconds: 5);

Future<bool> pingGameServer(String address, {Duration? timeout}) async {
  Future<bool> ping(String hostname, int port) async {
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

  final start = DateTime.now();
  var firstTime = true;
  final split = address.split(":");
  var hostname = split[0];
  if(isLocalHost(hostname)) {
    hostname = "127.0.0.1";
  }

  final port = int.parse(split.length > 1 ? split[1] : kDefaultGameServerPort);
  while (firstTime || (timeout != null && DateTime.now().millisecondsSinceEpoch - start.millisecondsSinceEpoch < timeout.inMilliseconds)) {
    final result = await ping(hostname, port)
        .timeout(_timeout, onTimeout: () => false);
    if(result) {
      return true;
    }

    if(firstTime) {
      firstTime = false;
    }else {
      await Future.delayed(_timeout);
    }
  }

  return false;
}