import 'dart:convert';
import 'dart:io';

import 'package:process_run/process_run.dart';
import 'package:reboot_common/common.dart';
import 'package:shelf/shelf_io.dart';
import 'package:shelf_proxy/shelf_proxy.dart';


final authenticatorLogFile = File("${logsDirectory.path}\\authenticator.log");
final authenticatorDirectory = Directory("${assetsDirectory.path}\\lawin");
final authenticatorExecutable = File("${authenticatorDirectory.path}\\run.bat");

Future<Process> startEmbeddedAuthenticator(bool detached) async {
  if(!authenticatorExecutable.existsSync()) {
    throw StateError("${authenticatorExecutable.path} doesn't exist");
  }

  var process = await Process.start(
      authenticatorExecutable.path,
      [],
      workingDirectory: authenticatorDirectory.path,
      mode: detached ? ProcessStartMode.detached : ProcessStartMode.normal
  );
  if(!detached) {
    authenticatorLogFile.createSync(recursive: true);
    process.outLines.forEach((element) => authenticatorLogFile.writeAsStringSync("$element\n", mode: FileMode.append));
    process.errLines.forEach((element) => authenticatorLogFile.writeAsStringSync("$element\n", mode: FileMode.append));
  }
  return process;
}

Future<HttpServer> startRemoteAuthenticatorProxy(Uri uri) async => await serve(proxyHandler(uri), kDefaultAuthenticatorHost, int.parse(kDefaultAuthenticatorPort));

Future<bool> isAuthenticatorPortFree() async => isPortFree(int.parse(kDefaultAuthenticatorPort));

Future<bool> freeAuthenticatorPort() async {
  var releaseBat = File("${assetsDirectory.path}\\lawin\\kill_lawin.bat");
  await Process.run(releaseBat.path, []);
  var standardResult = await isAuthenticatorPortFree();
  if(standardResult) {
    return true;
  }

  var elevatedResult = await runElevatedProcess(releaseBat.path, "");
  if(!elevatedResult) {
    return false;
  }

  return await isAuthenticatorPortFree();
}

Future<Uri?> pingSelf(String port) async => ping(kDefaultAuthenticatorHost, port);

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

