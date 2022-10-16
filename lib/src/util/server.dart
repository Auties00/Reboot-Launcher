import 'dart:convert';
import 'dart:io';

import 'package:archive/archive_io.dart';
import 'package:process_run/shell.dart';
import 'package:reboot_launcher/src/model/server_type.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:http/http.dart' as http;
import 'package:shelf_proxy/shelf_proxy.dart';
import 'package:shelf/shelf_io.dart';
import 'package:path/path.dart' as path;

final serverLocation = File("${Platform.environment["UserProfile"]}\\.reboot_launcher\\lawin\\Lawin.exe");
const String _serverUrl =
    "https://cdn.discordapp.com/attachments/1026121175878881290/1031230792069820487/LawinServer.zip";

Future<bool> downloadServer(ignored) async {
  var response = await http.get(Uri.parse(_serverUrl));
  var tempZip = File("${Platform.environment["Temp"]}/lawin.zip");
  await tempZip.writeAsBytes(response.bodyBytes);
  await extractFileToDisk(tempZip.path, serverLocation.parent.path);
  return true;
}

Future<bool> isLawinPortFree() async {
  try {
    var portBat = await loadBinary("port.bat", true);
    var process = await Process.run(portBat.path, []);
    return !process.outText.contains(" LISTENING ");
  }catch(_){
    return ServerSocket.bind("127.0.0.1", 3551)
        .then((socket) => socket.close())
        .then((_) => true)
        .onError((error, _) => false);
  }
}

Future<void> freeLawinPort() async {
  var releaseBat = await loadBinary("release.bat", false);
  await Process.run(releaseBat.path, []);
}

List<String> createRebootArgs(String username, bool headless) {
  var args = [
    "-skippatchcheck",
    "-epicapp=Fortnite",
    "-epicenv=Prod",
    "-epiclocale=en-us",
    "-epicportal",
    "-noeac",
    "-fromfl=be",
    "-fltoken=7ce411021b27b4343a44fdg8",
    "-caldera=eyJhbGciOiJFUzI1NiIsInR5cCI6IkpXVCJ9.eyJhY2NvdW50X2lkIjoiYmU5ZGE1YzJmYmVhNDQwN2IyZjQwZWJhYWQ4NTlhZDQiLCJnZW5lcmF0ZWQiOjE2Mzg3MTcyNzgsImNhbGRlcmFHdWlkIjoiMzgxMGI4NjMtMmE2NS00NDU3LTliNTgtNGRhYjNiNDgyYTg2IiwiYWNQcm92aWRlciI6IkVhc3lBbnRpQ2hlYXQiLCJub3RlcyI6IiIsImZhbGxiYWNrIjpmYWxzZX0.VAWQB67RTxhiWOxx7DBjnzDnXyyEnX7OljJm-j2d88G_WgwQ9wrE6lwMEHZHjBd1ISJdUO1UVUqkfLdU5nofBQ",
    "-AUTH_LOGIN=$username@projectreboot.dev",
    "-AUTH_PASSWORD=Rebooted",
    "-AUTH_TYPE=epic"
  ];

  if(headless){
    args.addAll(["-nullrhi", "-nosplash", "-nosound"]);
  }

  return args;
}

Future<Uri?> pingSelf() async => ping("127.0.0.1", "3551");

Future<Uri?> ping(String host, String port, [bool https=false]) async {
  var hostName = _getHostName(host);
  var declaredScheme = _getScheme(host);
  try{
    var uri = Uri(
        scheme: declaredScheme ?? (https ? "https" : "http"),
        host: hostName,
        port: int.parse(port)
    );
    var client = HttpClient()
      ..connectionTimeout = const Duration(seconds: 5);
    var request = await client.getUrl(uri);
    var response = await request.close();
    var body = utf8.decode(await response.single);
    return response.statusCode == 200 && body.contains("Welcome to LawinServer!") ? uri : null;
  }catch(_){
    return https || declaredScheme != null ? null : await ping(host, port, true);
  }
}

String? _getHostName(String host) => host.replaceFirst("http://", "").replaceFirst("https://", "");

String? _getScheme(String host) => host.startsWith("http://") ? "http" : host.startsWith("https://") ? "https" : null;

Future<ServerResult> checkServerPreconditions(String host, String port, ServerType type) async {
  host = host.trim();
  if(host.isEmpty){
    return ServerResult(
        type: ServerResultType.missingHostError
    );
  }

  port = port.trim();
  if(port.isEmpty){
    return ServerResult(
        type: ServerResultType.missingPortError
    );
  }

  if(int.tryParse(port) == null){
    return ServerResult(
        type: ServerResultType.illegalPortError
    );
  }

  if(type == ServerType.embedded || type == ServerType.remote){
    var free = await isLawinPortFree();
    if (!free) {
      return ServerResult(
          type: ServerResultType.portTakenError
      );
    }
  }

  if(type == ServerType.embedded && !serverLocation.existsSync()){
    return ServerResult(
        type: ServerResultType.serverDownloadRequiredError
    );
  }

  return ServerResult(
      uri: ping(host, port),
      type: ServerResultType.canStart
  );
}

Future<Process> startEmbeddedServer() async {
  return await Process.start(serverLocation.path, [], workingDirectory: serverLocation.parent.path);
}

Future<HttpServer> startRemoteServer(Uri uri) async {
  return await serve(proxyHandler(uri), "127.0.0.1", 3551);
}

class ServerResult {
  final Future<Uri?>? uri;
  final Object? error;
  final StackTrace? stackTrace;
  final ServerResultType type;

  ServerResult({this.uri, this.error, this.stackTrace, required this.type});
}

enum ServerResultType {
  missingHostError,
  missingPortError,
  illegalPortError,
  cannotPingServer,
  portTakenError,
  serverDownloadRequiredError,
  canStart,
  started,
  unknownError,
  stopped
}