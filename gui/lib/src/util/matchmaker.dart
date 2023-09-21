import 'dart:convert';
import 'dart:io';

import 'package:reboot_common/common.dart';

const Duration _timeout = Duration(seconds: 2);

Future<bool> _pingGameServer(String hostname, int port) async {
  var socket = await RawDatagramSocket.bind(InternetAddress.anyIPv4, 0);
  var dataToSend = utf8.encode(DateTime.now().toIso8601String());
  socket.send(dataToSend, InternetAddress(hostname), port);
  await for (var event in socket) {
    switch(event) {
      case RawSocketEvent.read:
        return true;
      case RawSocketEvent.readClosed:
      case RawSocketEvent.closed:
        return false;
      case RawSocketEvent.write:
        break;
    }
  }

  return false;
}

Future<bool> get _timeoutFuture => Future.delayed(_timeout).then((value) => false);

Future<bool> pingGameServer(String address, {Duration? timeout}) async {
  var start = DateTime.now();
  var firstTime = true;
  while (firstTime || (timeout != null && DateTime.now().millisecondsSinceEpoch - start.millisecondsSinceEpoch < timeout.inMilliseconds)) {
    var split = address.split(":");
    var hostname = split[0];
    if(isLocalHost(hostname)) {
      hostname = "127.0.0.1";
    }

    var port = int.parse(split.length > 1 ? split[1] : kDefaultGameServerPort);
    var result = await Future.any([_timeoutFuture, _pingGameServer(hostname, port)]);
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