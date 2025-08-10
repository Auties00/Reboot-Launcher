import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:reboot_common/common.dart';

class ServerBrowserClient {
  static const String _pingEvent = 'ping';
  static const String _addEvent = 'add';
  static const String _removeEvent = 'remove';
  static const Duration _pingInterval = const Duration(seconds: 30);
  static const Duration _reconnectDelay = const Duration(seconds: 10);
  static const Duration _timeout = const Duration(seconds: 10);
  
  final String _serverUrl;
  final StreamController<ServerBrowserEvent> _eventsController = StreamController.broadcast();
  
  WebSocket? _socket;
  Timer? _reconnectTimer;
  Timer? _pingTimer;
  Completer _pingCompleter = Completer();
  ServerBrowserState _state = ServerBrowserState.disconnected;

  ServerBrowserClient({required String serverUrl}) 
      : _serverUrl = serverUrl;
  
  Future<void> connect() async {
    if (_state != ServerBrowserState.disconnected) {
      return;
    }

    _setState(ServerBrowserState.connecting);
    _reconnectTimer?.cancel();

    try {
      final socket = await WebSocket.connect(_serverUrl)
          .timeout(_timeout);
      _socket = socket;
      _setState(ServerBrowserState.connected);
      socket.listen(
        _handleMessage,
        onDone: () => _handleDisconnection(_state == ServerBrowserState.connected),
        onError: (error) {
          _eventsController.add(new ServerBrowserErrorEvent(
              error: 'An unhandled error was thrown: $error'
          ));
          _handleDisconnection(true);
        }
      );
      _startHeartbeat();
    } catch (e) {
      _eventsController.add(new ServerBrowserErrorEvent(
          error: 'Cannot connect: $e'
      ));
      _handleDisconnection(true);
    }
  }

  void _handleMessage(dynamic data) {
    try {
      final message = jsonDecode(data);
      final type = message['type'];
      final payload = message['data'];
      switch (type) {
        case _pingEvent:
          if(!_pingCompleter.isCompleted) {
            _pingCompleter.complete(null);
          }
          break;

        case _addEvent:
          if (payload is List) {
            final entries = payload
                .map((entry) => ServerBrowserEntry.fromJson(entry))
                .toList(growable: false);
            _eventsController.add(new ServerBrowserAddEvent(
                entries: entries,
            ));
          }else {
            _eventsController.add(new ServerBrowserErrorEvent(
                error: 'Invalid add event payload: ${payload?.runtimeType}'
            ));
          }
          break;

        case _removeEvent:
          if (payload is List) {
            final entries = payload
                .map((entry) => entry['id'] as String?)
                .whereType<String>()
                .toList(growable: false);
            _eventsController.add(new ServerBrowserRemoveEvent(
                entries: entries
            ));
          }else {
            _eventsController.add(new ServerBrowserErrorEvent(
                error: 'Invalid remove event payload: ${payload?.runtimeType}'
            ));
          }
          break;

        default:
          _eventsController.add(new ServerBrowserErrorEvent(
              error: 'Invalid event type: $type'
          ));
          break;
      }
    } catch (error) {
      _eventsController.add(new ServerBrowserErrorEvent(
          error: 'An error occurred while processing an event: $error'
      ));
    }
  }

  void _handleDisconnection(bool reconnect) {
    _setState(ServerBrowserState.disconnected);
    _cleanup();
    if (reconnect) {
      _reconnectTimer = Timer(_reconnectDelay, () => connect());
    }
  }
  
  void _startHeartbeat() {
    _pingTimer?.cancel();
    _pingTimer = Timer.periodic(_pingInterval, (timer) async {
      final socket = _socket;
      if(socket == null || _state != ServerBrowserState.connected) {
        return;
      }

      try {
        socket.add(jsonEncode({'type': _pingEvent}));
        await _pingCompleter.future
            .timeout(_timeout);
        _pingCompleter = Completer();
        } catch (error) {
        _pingCompleter = Completer();
        _handleDisconnection(true);
      }
    });
  }

  void _cleanup() {
    _socket?.close();
    _socket = null;
    _pingTimer?.cancel();
    _pingTimer = null;
  }

  void _setState(ServerBrowserState newState) {
    if (_state != newState) {
      _state = newState;
      _eventsController.add(ServerBrowserStateEvent(
        state: newState
      ));
    }
  }

  Future<bool> addEntry(ServerBrowserEntry entry) async {
    if (_state != ServerBrowserState.connected) {
      return false;
    }

    final socket = _socket;
    if(socket == null) {
      return false;
    }

    final message = {
      'type': _addEvent,
      'data': entry.toJson()
    };
    socket.add(jsonEncode(message));
    return true;
  }

  Future<bool> removeEntry(String id) async {
    if (_state != ServerBrowserState.connected) {
      return false;
    }

    final socket = _socket;
    if(socket == null) {
      return false;
    }

    final message = {
      'type': _removeEvent,
      'data': id
    };
    socket.add(jsonEncode(message));
    return true;
  }

  StreamSubscription<ServerBrowserEvent> addListener(void Function(ServerBrowserEvent) onData) {
    return _eventsController.stream.listen(onData);
  }

  Future<void> disconnect() async {
    _reconnectTimer?.cancel();
    _cleanup();
    _setState(ServerBrowserState.disconnected);
  }

  void dispose() {
    disconnect();
    _eventsController.close();
  }
}