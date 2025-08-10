import 'package:server_browser_backend/server_browser_backend.dart';

void main() async {
  final server = ServerBrowserBackend();
  await server.start(port: 8080);
}