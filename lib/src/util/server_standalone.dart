import 'dart:io';

import 'package:archive/archive_io.dart';
import 'package:http/http.dart' as http;
import 'package:path/path.dart' as path;
import 'package:process_run/shell.dart';
import 'package:reboot_launcher/src/util/binary.dart';

final serverLocation = Directory("${Platform.environment["UserProfile"]}/.reboot_launcher/lawin");
const String _serverUrl =
    "https://github.com/Lawin0129/LawinServer/archive/refs/heads/main.zip";
const String _portableServerUrl =
    "https://cdn.discordapp.com/attachments/998020695223193673/1019999251994005504/LawinServer.exe";

Future<bool> downloadServer(bool portable) async {
  if(!portable){
    var response = await http.get(Uri.parse(_serverUrl));
    var tempZip = File("${Platform.environment["Temp"]}/lawin.zip");
    await tempZip.writeAsBytes(response.bodyBytes);
    await extractFileToDisk(tempZip.path, serverLocation.parent.path);
    var result = Directory("${serverLocation.parent.path}/LawinServer-main");
    await result.rename("${serverLocation.parent.path}/${path.basename(serverLocation.path)}");
    await Process.run("${serverLocation.path}/install_packages.bat", [], workingDirectory: serverLocation.path);
    await updateEngineConfig();
    return true;
  }

  var response = await http.get(Uri.parse(_portableServerUrl));
  var server = await loadBinary("LawinServer.exe", true);
  await server.writeAsBytes(response.bodyBytes);
  return true;
}

Future<void> updateEngineConfig() async {
  var engine = File("${serverLocation.path}/CloudStorage/DefaultEngine.ini");
  var patchedEngine = await loadBinary("DefaultEngine.ini", true);
  await engine.writeAsString(await patchedEngine.readAsString());
}

Future<bool> isLawinPortFree() async {
  return ServerSocket.bind("localhost", 3551)
      .then((socket) => socket.close())
      .then((_) => true)
      .onError((error, _) => false);
}

List<String> createRebootArgs(String username, bool headless) {
  var args = [
    "-epicapp=Fortnite",
    "-epicenv=Prod",
    "-epiclocale=en-us",
    "-epicportal",
    "-skippatchcheck",
    "-fromfl=eac",
    "-fltoken=3db3ba5dcbd2e16703f3978d",
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
    return response.statusCode == 200 ? uri : null;
  }catch(_){
    return https || declaredScheme != null ? null : await ping(host, port, true);
  }
}

String? _getHostName(String host) => host.replaceFirst("http://", "").replaceFirst("https://", "");

String? _getScheme(String host) => host.startsWith("http://") ? "http" : host.startsWith("https://") ? "https" : null;