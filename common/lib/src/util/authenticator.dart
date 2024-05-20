import 'dart:io';

import 'package:reboot_common/common.dart';
import 'package:shelf/shelf_io.dart';
import 'package:shelf_proxy/shelf_proxy.dart';

final authenticatorDirectory = Directory("${assetsDirectory.path}\\authenticator");
final authenticatorStartExecutable = File("${authenticatorDirectory.path}\\lawinserver.exe");

Future<int> startEmbeddedAuthenticator(bool detached) async => startBackgroundProcess(
    executable: authenticatorStartExecutable,
    window: detached
);

Future<HttpServer> startRemoteAuthenticatorProxy(Uri uri) async {
  print("CALLED: $uri");
  return await serve(proxyHandler(uri), kDefaultAuthenticatorHost, kDefaultAuthenticatorPort);
}

Future<bool> isAuthenticatorPortFree() async => await pingAuthenticator(kDefaultAuthenticatorHost, kDefaultAuthenticatorPort.toString()) == null;

Future<bool> freeAuthenticatorPort() async {
  await killProcessByPort(kDefaultAuthenticatorPort);
  final standardResult = await isAuthenticatorPortFree();
  if(standardResult) {
    return true;
  }

  return false;
}

Future<Uri?> pingAuthenticator(String host, String port, [bool https=false]) async {
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
    return response.statusCode == 200 || response.statusCode == 404 ? uri : null;
  }catch(_){
    return https || declaredScheme != null || isLocalHost(host) ? null : await pingAuthenticator(host, port, true);
  }
}

String? _getHostName(String host) => host.replaceFirst("http://", "").replaceFirst("https://", "");

String? _getScheme(String host) => host.startsWith("http://") ? "http" : host.startsWith("https://") ? "https" : null;

