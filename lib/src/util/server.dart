import 'dart:convert';
import 'dart:io';
import 'dart:math';

import 'package:http/http.dart' as http;
import 'package:ini/ini.dart';
import 'package:process_run/shell.dart';
import 'package:reboot_launcher/src/model/server_type.dart';
import 'package:reboot_launcher/src/ui/controller/game_controller.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:shelf/shelf_io.dart';
import 'package:shelf_proxy/shelf_proxy.dart';

final serverLogFile = File("${logsDirectory.path}\\server.log");
final serverDirectory = Directory("${assetsDirectory.path}\\lawin");
final serverExeFile = File("${serverDirectory.path}\\lawinserver-win.exe");

Future<void> writeMatchmakingIp(String text) async {
  var file = File("${assetsDirectory.path}\\lawin\\Config\\config.ini");
  if(!file.existsSync()){
    return;
  }

  var splitIndex = text.indexOf(":");
  var ip = splitIndex != -1 ? text.substring(0, splitIndex) : text;
  var port = splitIndex != -1 ? text.substring(splitIndex + 1) : "7777";
  var config = Config.fromString(file.readAsStringSync());
  config.set("GameServer", "ip", ip);
  config.set("GameServer", "port", port);
  file.writeAsStringSync(config.toString());
}

Future<void> startServer(bool detached) async {
  var process = await Process.start(
      serverExeFile.path,
      [],
      workingDirectory: serverDirectory.path,
      mode: detached ? ProcessStartMode.detached : ProcessStartMode.normal,
      runInShell: detached
  );
  if(!detached) {
    serverLogFile.createSync(recursive: true);
    process.outLines.forEach((element) =>
        serverLogFile.writeAsStringSync("$element\n", mode: FileMode.append));
    process.errLines.forEach((element) =>
        serverLogFile.writeAsStringSync("$element\n", mode: FileMode.append));
  }
}

Future<void> stopServer() async {
  await freeLawinPort();
  await freeMatchmakerPort();
}

Future<bool> isLawinPortFree() async {
  return http.get(Uri.parse("http://127.0.0.1:3551/unknown"))
      .timeout(const Duration(milliseconds: 500))
      .then((value) => false)
      .onError((error, stackTrace) => true);
}

Future<bool> isMatchmakerPortFree() async {
  return HttpServer.bind("127.0.0.1", 8080)
      .then((socket) => socket.close())
      .then((_) => true)
      .onError((error, _) => false);
}

Future<void> freeLawinPort() async {
  var releaseBat = File("${assetsDirectory.path}\\lawin\\kill_lawin.bat");
  await Process.run(releaseBat.path, []);
}

Future<void> freeMatchmakerPort() async {
  var releaseBat = File("${assetsDirectory.path}\\lawin\\kill_matchmaker.bat");
  await Process.run(releaseBat.path, []);
}

Future<void> resetWinNat() async {
  var binary = File("${serverDirectory.path}\\winnat.bat");
  await runElevated(binary.path, "");
}

List<String> createRebootArgs(String username, String password, bool host, String additionalArgs) {
  if(password.isEmpty) {
    username = username.isEmpty ? kDefaultPlayerName : username;
    username = host ? "$username${Random().nextInt(1000)}" : username;
    username = '$username@projectreboot.dev';
  }
  password = password.isNotEmpty ? password : "Rebooted";
  var args = [
    "-epicapp=Fortnite",
    "-epicenv=Prod",
    "-epiclocale=en-us",
    "-epicportal",
    "-skippatchcheck",
    "-nobe",
    "-fromfl=eac",
    "-fltoken=3db3ba5dcbd2e16703f3978d",
    "-caldera=eyJhbGciOiJFUzI1NiIsInR5cCI6IkpXVCJ9.eyJhY2NvdW50X2lkIjoiYmU5ZGE1YzJmYmVhNDQwN2IyZjQwZWJhYWQ4NTlhZDQiLCJnZW5lcmF0ZWQiOjE2Mzg3MTcyNzgsImNhbGRlcmFHdWlkIjoiMzgxMGI4NjMtMmE2NS00NDU3LTliNTgtNGRhYjNiNDgyYTg2IiwiYWNQcm92aWRlciI6IkVhc3lBbnRpQ2hlYXQiLCJub3RlcyI6IiIsImZhbGxiYWNrIjpmYWxzZX0.VAWQB67RTxhiWOxx7DBjnzDnXyyEnX7OljJm-j2d88G_WgwQ9wrE6lwMEHZHjBd1ISJdUO1UVUqkfLdU5nofBQ",
    "-AUTH_LOGIN=$username",
    "-AUTH_PASSWORD=${password.isNotEmpty ? password : "Rebooted"}",
    "-AUTH_TYPE=epic"
  ];

  if(host){
    args.addAll([
      "-nullrhi",
      "-nosplash",
      "-nosound",
    ]);
  }

  if(additionalArgs.isNotEmpty){
    args.addAll(additionalArgs.split(" "));
  }

  return args;
}

Future<Uri?> pingSelf(String port) async => ping("127.0.0.1", port);

Future<Uri?> ping(String host, String port, [bool https=false]) async {
  var hostName = _getHostName(host);
  var declaredScheme = _getScheme(host);
  try{
    var uri = Uri(
        scheme: declaredScheme ?? (https ? "https" : "http"),
        host: hostName,
        port: int.parse(port),
        path: "unknown"
    );
    var client = HttpClient()
      ..connectionTimeout = const Duration(seconds: 5);
    var request = await client.getUrl(uri);
    var response = await request.close();
    var body = utf8.decode(await response.single);
    return body.contains("epicgames") || body.contains("lawinserver") ? uri : null;
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

  var portNumber = int.tryParse(port);
  if(portNumber == null){
    return ServerResult(
        type: ServerResultType.illegalPortError
    );
  }

  if(isLocalHost(host) && portNumber == 3551 && type == ServerType.remote){
    return ServerResult(
        type: ServerResultType.illegalPortError
    );
  }

  if(type != ServerType.local && !(await isLawinPortFree())){
    return ServerResult(
        type: ServerResultType.backendPortTakenError
    );
  }

  if(type == ServerType.embedded && !(await isMatchmakerPortFree())){
    return ServerResult(
        type: ServerResultType.backendPortTakenError
    );
  }

  return ServerResult(
      type: ServerResultType.canStart
  );
}

Future<HttpServer> startRemoteServer(Uri uri) async => await serve(proxyHandler(uri), "127.0.0.1", 3551);

class ServerResult {
  final int? pid;
  final Object? error;
  final StackTrace? stackTrace;
  final ServerResultType type;

  ServerResult({this.pid, this.error, this.stackTrace, required this.type});
}

enum ServerResultType {
  missingHostError,
  missingPortError,
  illegalPortError,
  cannotPingServer,
  backendPortTakenError,
  matchmakerPortTakenError,
  canStart,
  alreadyStarted,
  unknownError,
  stopped
}