import 'dart:async';

import 'package:get/get.dart';
import 'package:get/get_state_manager/src/simple/get_controllers.dart';
import 'package:reboot_common/common.dart';
import 'package:sync/semaphore.dart';

final class ServerBrowserController extends GetxController {
  static const String _url = "ws://192.99.216.42:8080";

  final Rxn<List<ServerBrowserEntry>> servers;
  final Map<String, ServerBrowserEntry> _entries;
  final ServerBrowserClient _client;
  final Semaphore _semaphore;

  ServerBrowserController() :
        servers = Rxn(),
        _entries = {},
        _client = ServerBrowserClient(serverUrl: _url)..connect(), // The client should always be connected
        _semaphore = Semaphore() {
    addEventsListener((data) {
      switch(data) {
        case ServerBrowserStateEvent():
          break;
        case ServerBrowserAddEvent():
          for(final entry in data.entries) {
            _entries[entry.id] = entry;
          }
          _updateServers();
          break;
        case ServerBrowserRemoveEvent():
          for(final entry in data.entries) {
            _entries.remove(entry);
          }
          _updateServers();
          break;
        case ServerBrowserErrorEvent():
          break;
      }
    });
  }

  void _updateServers() {
    servers.value = servers.value == null
        ? _entries.values.toList(growable: false)
        : [...?servers.value, ..._entries.values];
  }

  Future<void> addServer(ServerBrowserEntry entry) async {
    try {
      _semaphore.acquire();
      await _client.addEntry(entry);
    } finally {
      _semaphore.release();
    }
  }

  Future<void> removeServer(String uuid) async {
    try {
      _semaphore.acquire();
      await _client.removeEntry(uuid);
    } finally {
      _semaphore.release();
    }
  }

  StreamSubscription<ServerBrowserEvent> addEventsListener(void Function(ServerBrowserEvent) onData) {
    return _client.addListener(onData);
  }

  ServerBrowserEntry? getServerById(String uuid) {
    return _entries[uuid];
  }
}