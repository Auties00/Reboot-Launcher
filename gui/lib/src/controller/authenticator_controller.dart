import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';

class AuthenticatorController extends ServerController {
  AuthenticatorController() : super();

  @override
  String get controllerName => "authenticator";

  @override
  String get storageName => "authenticator";

  @override
  String get defaultHost => kDefaultAuthenticatorHost;

  @override
  String get defaultPort => kDefaultAuthenticatorPort;

  @override
  Future<bool> get isPortFree => isAuthenticatorPortFree();

  @override
  Future<bool> freePort() => freeAuthenticatorPort();

  @override
  Future<int> startEmbeddedInternal() => startEmbeddedAuthenticator(detached.value);

  @override
  Future<Uri?> pingServer(String host, String port) => pingAuthenticator(host, port);
}