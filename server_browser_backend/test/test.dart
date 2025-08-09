import 'dart:convert';
import 'dart:io';
import 'package:server_browser_backend/server_browser_backend.dart';
import 'package:test/test.dart';

void main() {
  group('WebSocket Server Tests', () {
    late WebSocketServer server;
    final int testPort = 8081;

    setUp(() async {
      server = WebSocketServer();
      await server.start(port: testPort);
    });

    tearDown(() async {
      await server.stop();
    });

    test('should allow client connection and receive initial empty data', () async {
      final client = await WebSocket.connect('ws://localhost:$testPort');
      final messagesFuture = client.toList();

      await client.close();

      final messages = await messagesFuture;

      expect(messages.length, equals(1));

      final firstMessage = jsonDecode(messages[0]);
      expect(firstMessage['type'], equals('add'));
      expect(firstMessage['data'], equals([]));
    });

    test('should add server entry and broadcast to all clients', () async {
      final client1 = await WebSocket.connect('ws://localhost:$testPort');
      final client1MessagesFuture = client1.toList();
      final client2 = await WebSocket.connect('ws://localhost:$testPort');
      final client2MessagesFuture = client2.toList();

      final testEntry = {
        'id': 'test-server-1',
        'name': 'Test Server',
        'description': 'A test server',
        'version': '1.0.0',
        'password': 'secret123',
        'timestamp': DateTime.now().toIso8601String(),
        'ip': '127.0.0.1',
        'author': 'Test Author',
        'discoverable': true,
      };

      final addMessage = {
        'type': 'add',
        'data': testEntry,
      };

      client1.add(jsonEncode(addMessage));

      await client1.close();
      await client2.close();

      final client1Messages = await client1MessagesFuture;
      final client2Messages = await client2MessagesFuture;

      expect(client1Messages.length, equals(2));
      expect(client2Messages.length, equals(2));

      final client1Initial = jsonDecode(client1Messages[0]);
      final client2Initial = jsonDecode(client2Messages[0]);
      expect(client1Initial['type'], equals('add'));
      expect(client1Initial['data'], equals([]));
      expect(client2Initial['type'], equals('add'));
      expect(client2Initial['data'], equals([]));

      final client1Broadcast = jsonDecode(client1Messages[1]);
      final client2Broadcast = jsonDecode(client2Messages[1]);

      expect(client1Broadcast['type'], equals('add'));
      expect(client1Broadcast['data'], isA<List>());
      expect(client1Broadcast['data'].length, equals(1));
      expect(client1Broadcast['data'][0]['id'], equals('test-server-1'));

      expect(client2Broadcast['type'], equals('add'));
      expect(client2Broadcast['data'], isA<List>());
      expect(client2Broadcast['data'].length, equals(1));
      expect(client2Broadcast['data'][0]['id'], equals('test-server-1'));
    });

    test('should send existing entries to new client', () async {
      final client1 = await WebSocket.connect('ws://localhost:$testPort');
      final client1MessagesFuture = client1.toList();

      final testEntry = {
        'id': 'existing-server',
        'name': 'Existing Server',
        'description': 'Already exists',
        'version': '2.0.0',
        'password': 'existing123',
        'timestamp': DateTime.now().toIso8601String(),
        'ip': '192.168.1.1',
        'author': 'Existing Author',
        'discoverable': false,
      };

      final addMessage = {
        'type': 'add',
        'data': testEntry,
      };

      client1.add(jsonEncode(addMessage));

      final client2 = await WebSocket.connect('ws://localhost:$testPort');
      final client2MessagesFuture = client2.toList();

      await client1.close();
      await client2.close();

      final client1Messages = await client1MessagesFuture;
      final client2Messages = await client2MessagesFuture;

      expect(client1Messages.length, equals(2));
      expect(client2Messages.length, equals(1));

      final client2InitialMessage = jsonDecode(client2Messages[0]);
      expect(client2InitialMessage['type'], equals('add'));
      expect(client2InitialMessage['data'], isA<List>());
      expect(client2InitialMessage['data'].length, equals(1));
      expect(client2InitialMessage['data'][0]['id'], equals('existing-server'));
    });

    test('should remove server entry and broadcast removal', () async {
      final client1 = await WebSocket.connect('ws://localhost:$testPort');
      final client1MessagesFuture = client1.toList();
      final client2 = await WebSocket.connect('ws://localhost:$testPort');
      final client2MessagesFuture = client2.toList();

      final testEntry = {
        'id': 'server-to-remove',
        'name': 'Server To Remove',
        'description': 'Will be removed',
        'version': '1.0.0',
        'password': 'remove123',
        'timestamp': DateTime.now().toIso8601String(),
        'ip': '10.0.0.1',
        'author': 'Remove Author',
        'discoverable': true,
      };

      final addMessage = {
        'type': 'add',
        'data': testEntry,
      };
      client1.add(jsonEncode(addMessage));

      final removeMessage = {
        'type': 'remove',
        'data': 'server-to-remove',
      };
      client1.add(jsonEncode(removeMessage));

      await client1.close();
      await client2.close();

      final client1Messages = await client1MessagesFuture;
      final client2Messages = await client2MessagesFuture;

      expect(client1Messages.length, equals(3));
      expect(client2Messages.length, equals(3));

      final client1RemoveResponse = jsonDecode(client1Messages[2]);
      final client2RemoveResponse = jsonDecode(client2Messages[2]);

      expect(client1RemoveResponse['type'], equals('remove'));
      expect(client1RemoveResponse['data'], isA<List>());
      expect(client1RemoveResponse['data'].length, equals(1));
      expect(client1RemoveResponse['data'][0]['id'], equals('server-to-remove'));

      expect(client2RemoveResponse['type'], equals('remove'));
      expect(client2RemoveResponse['data'], isA<List>());
      expect(client2RemoveResponse['data'].length, equals(1));
      expect(client2RemoveResponse['data'][0]['id'], equals('server-to-remove'));
    });

    test('should handle removal of non-existent entry', () async {
      final client = await WebSocket.connect('ws://localhost:$testPort');
      final messagesFuture = client.toList();

      final removeMessage = {
        'type': 'remove',
        'data': 'non-existent-id',
      };

      client.add(jsonEncode(removeMessage));

      await client.close();
      final messages = await messagesFuture;

      expect(messages.length, equals(2));

      final response = jsonDecode(messages[1]);
      expect(response['type'], equals('remove'));
      expect(response['success'], equals(false));
      expect(response['message'], equals('Invalid server entry'));
    });

    test('should handle unknown message type', () async {
      final client = await WebSocket.connect('ws://localhost:$testPort');
      final messagesFuture = client.toList();

      final unknownMessage = {
        'type': 'unknown',
        'data': {'some': 'data'},
      };

      client.add(jsonEncode(unknownMessage));

      await client.close();
      final messages = await messagesFuture;

      expect(messages.length, equals(2));

      final response = jsonDecode(messages[1]);
      expect(response['type'], equals('unknown'));
      expect(response['success'], equals(false));
      expect(response['message'], equals('Unknown type'));
    });

    test('should handle invalid JSON', () async {
      final client = await WebSocket.connect('ws://localhost:$testPort');
      final messagesFuture = client.toList();

      client.add('invalid json string');

      await client.close();
      final messages = await messagesFuture;

      expect(messages.length, equals(2));

      final response = jsonDecode(messages[1]);
      expect(response['success'], equals(false));
      expect(response.containsKey('message'), isTrue);
    });

    test('should handle multiple clients adding different entries', () async {
      final client1 = await WebSocket.connect('ws://localhost:$testPort');
      final client1MessagesFuture = client1.toList();
      final client2 = await WebSocket.connect('ws://localhost:$testPort');
      final client2MessagesFuture = client2.toList();
      final client3 = await WebSocket.connect('ws://localhost:$testPort');
      final client3MessagesFuture = client3.toList();

      final entry1 = {
        'id': 'server-1',
        'name': 'Server One',
        'description': 'First server',
        'version': '1.0.0',
        'password': 'pass1',
        'timestamp': DateTime.now().toIso8601String(),
        'ip': '192.168.1.1',
        'author': 'Author 1',
        'discoverable': true,
      };

      final entry2 = {
        'id': 'server-2',
        'name': 'Server Two',
        'description': 'Second server',
        'version': '2.0.0',
        'password': 'pass2',
        'timestamp': DateTime.now().toIso8601String(),
        'ip': '192.168.1.2',
        'author': 'Author 2',
        'discoverable': false,
      };

      client1.add(jsonEncode({'type': 'add', 'data': entry1}));
      client2.add(jsonEncode({'type': 'add', 'data': entry2}));

      await Future.delayed(Duration(milliseconds: 200));

      await client1.close();
      await client2.close();
      await client3.close();

      final client1Messages = await client1MessagesFuture;
      final client2Messages = await client2MessagesFuture;
      final client3Messages = await client3MessagesFuture;

      expect(client1Messages.length, equals(3));
      expect(client2Messages.length, equals(3));
      expect(client3Messages.length, equals(3));

      final allServerIds = <String>{};

      for (final messages in [client1Messages, client2Messages, client3Messages]) {
        for (int i = 1; i < messages.length; i++) {
          final parsed = jsonDecode(messages[i]);
          if (parsed['type'] == 'add' && parsed['data'] is List) {
            for (final entry in parsed['data']) {
              allServerIds.add(entry['id']);
            }
          }
        }
      }

      expect(allServerIds, containsAll(['server-1', 'server-2']));
    });

    test('should handle client disconnection gracefully', () async {
      final client1 = await WebSocket.connect('ws://localhost:$testPort');
      final client1MessagesFuture = client1.toList();
      final client2 = await WebSocket.connect('ws://localhost:$testPort');
      final client2MessagesFuture = client2.toList();

      await client1.close();
      final client1Messages = await client1MessagesFuture;
      expect(client1Messages.length, equals(1));

      final testEntry = {
        'id': 'after-disconnect',
        'name': 'After Disconnect',
        'description': 'Added after client disconnect',
        'version': '1.0.0',
        'password': 'disconnect123',
        'timestamp': DateTime.now().toIso8601String(),
        'ip': '172.16.0.1',
        'author': 'Disconnect Author',
        'discoverable': true,
      };

      client2.add(jsonEncode({'type': 'add', 'data': testEntry}));

      await client2.close();
      final client2Messages = await client2MessagesFuture;

      expect(client2Messages.length, equals(2));

      final response = jsonDecode(client2Messages[1]);
      expect(response['type'], equals('add'));
      expect(response['data'][0]['id'], equals('after-disconnect'));
    });

    test('should handle ServerEntry serialization correctly', () async {
      final client = await WebSocket.connect('ws://localhost:$testPort');
      final messagesFuture = client.toList();

      final testEntry = {
        'id': 'serialization-test',
        'name': 'Serialization Test',
        'description': 'Testing serialization',
        'version': '3.1.4',
        'password': 'serialize123',
        'timestamp': DateTime.now().toIso8601String(),
        'ip': '203.0.113.1',
        'author': 'Serialization Author',
        'discoverable': true,
      };

      client.add(jsonEncode({'type': 'add', 'data': testEntry}));

      await client.close();
      final messages = await messagesFuture;

      expect(messages.length, equals(2));

      final broadcastMessage = jsonDecode(messages[1]);
      final receivedEntry = broadcastMessage['data'][0];

      expect(receivedEntry['id'], equals('serialization-test'));
      expect(receivedEntry['name'], equals('Serialization Test'));
      expect(receivedEntry['description'], equals('Testing serialization'));
      expect(receivedEntry['version'], equals('3.1.4'));
      expect(receivedEntry['password'], equals('serialize123'));
      expect(receivedEntry['ip'], equals('203.0.113.1'));
      expect(receivedEntry['author'], equals('Serialization Author'));
      expect(receivedEntry['discoverable'], equals(true));
      expect(receivedEntry['timestamp'], isA<String>());

      expect(() => DateTime.parse(receivedEntry['timestamp']), returnsNormally);
    });

    test('should handle rapid sequential operations', () async {
      final client1 = await WebSocket.connect('ws://localhost:$testPort');
      final client1MessagesFuture = client1.toList();
      final client2 = await WebSocket.connect('ws://localhost:$testPort');
      final client2MessagesFuture = client2.toList();

      final entry1 = {
        'id': 'rapid-1',
        'name': 'Rapid Server 1',
        'description': 'First rapid server',
        'version': '1.0.0',
        'password': 'rapid123',
        'timestamp': DateTime.now().toIso8601String(),
        'ip': '10.0.0.1',
        'author': 'Rapid Author',
        'discoverable': true,
      };

      final entry2 = {
        'id': 'rapid-2',
        'name': 'Rapid Server 2',
        'description': 'Second rapid server',
        'version': '1.0.1',
        'password': 'rapid456',
        'timestamp': DateTime.now().toIso8601String(),
        'ip': '10.0.0.2',
        'author': 'Rapid Author',
        'discoverable': false,
      };

      client1.add(jsonEncode({'type': 'add', 'data': entry1}));
      client1.add(jsonEncode({'type': 'add', 'data': entry2}));
      client1.add(jsonEncode({'type': 'remove', 'data': 'rapid-1'}));

      await Future.delayed(Duration(milliseconds: 200));

      await client1.close();
      await client2.close();

      final client1Messages = await client1MessagesFuture;
      final client2Messages = await client2MessagesFuture;

      expect(client1Messages.length, equals(4));
      expect(client2Messages.length, equals(4));

      final lastAddMessage = jsonDecode(client1Messages[2]);
      expect(lastAddMessage['type'], equals('add'));
      expect(lastAddMessage['data'][0]['id'], equals('rapid-2'));

      final removeMessage = jsonDecode(client1Messages[3]);
      expect(removeMessage['type'], equals('remove'));
      expect(removeMessage['data'][0]['id'], equals('rapid-1'));
    });
  });
}