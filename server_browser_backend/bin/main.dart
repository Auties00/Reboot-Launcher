import 'package:server_browser_backend/server_browser_backend.dart';

void main() async {
  final server = WebSocketServer();
  await server.start(port: 8080);
}