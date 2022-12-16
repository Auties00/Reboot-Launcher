
import 'package:jaguar/http/context/context.dart';
import 'package:jaguar/http/response/response.dart';
import 'package:reboot_launcher/src/util/os.dart';

final String _keyChain = loadEmbedded("responses/keychain.json").readAsStringSync();
final String _catalog = loadEmbedded("responses/catalog.json").readAsStringSync();

Response getCatalog(Context context) {
  if (context.headers.value("user-agent")?.contains("2870186") == true) {
    return Response(statusCode: 404);
  }

  return Response(body: _catalog, headers: {"content-type": "application/json"});
}

Response getKeyChain(Context context) => Response(body: _keyChain, headers: {"content-type": "application/json"});

Map<String, Object> getOffers(Context context) => {};