import 'dart:convert';
import 'dart:io';

import 'package:server_browser_backend/src/server_entry.dart';

class WebSocketServer {
  static const String addEvent = 'add';
  static const String removeEvent = 'remove';

  final Map<String, ServerEntry> _entries = {};
  final Set<WebSocket> _clients = {};

  late HttpServer _server;

  Future<void> start({int port = 8080}) async {
    _server = await HttpServer.bind('0.0.0.0', port);
    _listen();
  }

  Future<void> _listen() async {
     await for (HttpRequest request in _server) {
      if (WebSocketTransformer.isUpgradeRequest(request)) {
        _handleWebSocketUpgrade(request);
      } else {
        request.response.statusCode = 404;
        request.response.close();
      }
    }
  }

  Future<void> _handleWebSocketUpgrade(HttpRequest request) async {
    final client = await WebSocketTransformer.upgrade(request);
    _clients.add(client);
    await _sendAllEntriesToClient(client);
    client.listen(
          (message) => _handleMessage(client, message),
          onDone: () => _removeClient(client),
          onError: (error) => _removeClient(client)
    );
  }

  Future<void> _sendAllEntriesToClient(WebSocket client) async {
    final message = {
      'type': addEvent,
      'data': _entries.values.map((entry) => entry.toJson()).toList()
    };
    client.add(json.encode(message));
  }

  void _handleMessage(WebSocket client, dynamic message) {
    String? type;
    try {
      final data = jsonDecode(message);
      type = data['type'];
      final payload = data['data'];
      switch (type) {
        case addEvent:
          final entry = ServerEntry.fromJson(payload);
          _entries[entry.id] = entry;
          _broadcastEvent(addEvent, entry.toJson());
          break;
        case removeEvent:
          final deletedEntry = _entries.remove(payload);
          if (deletedEntry != null) {
            _broadcastEvent(removeEvent, {'id': deletedEntry.id});
          }else {
            _answer(client, removeEvent, "Invalid server entry");
          }
          break;
        default:
          _answer(client, type, 'Unknown type');
          break;
      }
    } catch(error) {
      _answer(client, type, error.toString());
    }
  }

  void _broadcastEvent(String eventType, Map<String, dynamic> eventData) {
    final message = {
      'type': eventType,
      'data': [
        eventData
      ]
    };

    final messageJson = json.encode(message);
    final clientsToRemove = <WebSocket>[];

    for (final client in _clients) {
      try {
        client.add(messageJson);
      } catch (e) {
        clientsToRemove.add(client);
      }
    }

    for (final client in clientsToRemove) {
      _removeClient(client);
    }
  }

  void _removeClient(WebSocket client) {
    client.close();
    _clients.remove(client);
  }

  void _answer(WebSocket client, String? eventType, [String? error]) {
    final message = {};
    if(eventType != null) {
      message['type'] = eventType;
    }
    if(error != null) {
      message['success'] = false;
      message['message'] = error;
    }else {
      message['success'] = true;
    }
    client.add(json.encode(message));
  }

  Future<void> stop() async {
    await _server.close(force: true);
    _clients.clear();
  }
}