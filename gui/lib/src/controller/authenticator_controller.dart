import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/util/translations.dart';

class AuthenticatorController extends ServerController {
  AuthenticatorController() : super();

  @override
  String get controllerName => translations.authenticatorName.toLowerCase();

  @override
  String get storageName => "authenticator";

  @override
  String get defaultHost => kDefaultAuthenticatorHost;

  @override
  String get defaultPort => kDefaultAuthenticatorPort.toString();

  @override
  Future<bool> get isPortFree => isAuthenticatorPortFree();

  @override
  Future<bool> freePort() => freeAuthenticatorPort();

  @override
  RebootPageType get pageType => RebootPageType.authenticator;

  @override
  Future<int> startEmbeddedInternal() => startEmbeddedAuthenticator(detached.value);

  @override
  Future<Uri?> pingServer(String host, String port) => pingAuthenticator(host, port);
}