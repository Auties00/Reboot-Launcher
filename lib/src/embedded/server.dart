import "dart:async";
import "dart:io";

import "package:jaguar/jaguar.dart";
import "package:reboot_launcher/src/embedded/auth.dart";
import 'package:reboot_launcher/src/embedded/misc.dart';
import 'package:reboot_launcher/src/embedded/privacy.dart';
import "package:reboot_launcher/src/embedded/storage.dart";
import 'package:reboot_launcher/src/embedded/storefront.dart';
import "package:reboot_launcher/src/embedded/version.dart";

import '../util/server.dart';
import "error.dart";
import "lightswitch.dart";
import 'matchmaking.dart';

bool _loggingCapabilities = false;

Future<Jaguar> startEmbeddedServer(String Function() ipQuery) async {
  var server = _createServer(ipQuery);
  await server.serve(logRequests: true);
  return server;
}

Future<Jaguar> startEmbeddedMatchmaker() async {
  var server = _createMatchmaker();
  server.serve(logRequests: true);
  return server;
}

Jaguar _createServer(String Function() ipQuery) {
  var server = Jaguar(address: "127.0.0.1", port: 3551, errorWriter: EmbeddedErrorWriter());

  // Version
  server.getJson("/fortnite/api/version", getVersion);
  server.getJson("/fortnite/api/v2/versioncheck/*", hasUpdate);
  server.getJson("/fortnite/api/v2/versioncheck*", hasUpdate);
  server.getJson("/fortnite/api/versioncheck*", hasUpdate);

  // Auth
  server.getJson("/account/api/public/account/displayName/:accountId", getAccountInfo);
  server.getJson("/account/api/public/account/:accountId", getAccountInfo);
  server.getJson("/account/api/public/account/:accountId/externalAuths", getExternalAuths);
  server.getJson("/account/api/public/account", getAccounts);
  server.delete("/account/api/oauth/sessions/kill/*", (context) => Response(statusCode: 204));
  server.getJson("/account/api/oauth/verify", verifyOAuthToken);
  server.postJson("/account/api/oauth/token", getOAuthToken);
  server.postJson("/account/api/oauth/exchange", getExchange);
  server.getJson("/account/api/epicdomains/ssodomains", getSsoDomains);
  server.post("/fortnite/api/game/v2/tryPlayOnPlatform/account/*", tryPlayOnPlatform);
  server.post("/datarouter/api/v1/public/data/*", (context) => Response(statusCode: 204));
  server.getJson("/fortnite/api/game/v2/enabled_features", getFeatures);
  server.postJson("/fortnite/api/game/v2/grant_access/*", (context) => Response(statusCode: 204));
  server.postJson("/fortnite/api/game/v2/profile/:profileId/client/EquipBattleRoyaleCustomization", equipItem);
  server.postJson("/fortnite/api/game/v2/profile/:profileId/client/*", getProfile);

  // Storage
  server.getJson("/fortnite/api/cloudstorage/system", getStorageSettings);
  server.get("/fortnite/api/cloudstorage/system/:file", getStorageSetting);
  server.getJson("/fortnite/api/cloudstorage/user/:accountId", getStorageAccount);
  server.getJson("/fortnite/api/cloudstorage/user/:accountId/:file", getStorageFile);
  server.put("/fortnite/api/cloudstorage/user/:accountId/:file", addStorageFile);

  // Status
  server.getJson("/lightswitch/api/service/Fortnite/status", getFortniteStatus);
  server.getJson("/lightswitch/api/service/bulk/status", getBulkStatus);

  // Keychain and catalog
  server.get("/fortnite/api/storefront/v2/catalog", getCatalog);
  server.get("/fortnite/api/storefront/v2/keychain", getKeyChain);
  server.get("/catalog/api/shared/bulk/offers", getOffers);

  // Matchmaking
  server.get("/fortnite/api/matchmaking/session/findPlayer/*", (context) => Response(statusCode: 200));
  server.getJson("/fortnite/api/game/v2/matchmakingservice/ticket/player/*", getPlayerTicket);
  server.getJson("/fortnite/api/game/v2/matchmaking/account/:accountId/session/:sessionId", getSessionAccount);
  server.getJson("/fortnite/api/matchmaking/session/:sessionId", (context) => getMatch(context, ipQuery));
  server.post("/fortnite/api/matchmaking/session/:accountId/join", (context) => Response(statusCode: 204));
  server.postJson("/fortnite/api/matchmaking/session/matchMakingRequest", (context) => getMatchmakingRequests);

  // Misc
  server.getJson("/api/v1/events/Fortnite/download/*", getDownload);
  server.getJson("/fortnite/api/receipts/v1/account/:accountId/receipts", getReceipts);
  server.getJson("/content/api/pages/*", getContentPages);
  server.getJson("/friends/api/v1/:accountId/settings", getFriendsSettings);
  server.getJson("/friends/api/v1/:accountId/blocklist", getFriendsBlocklist);
  server.getJson("/friends/api/public/blocklist/:accountId", getFriendsBlocklist);
  server.getJson("/friends/api/public/friends/:accountId", getFriendsList);
  server.getJson("/friends/api/public/list/fortnite/:accountId/recentPlayers", getRecentPlayers);
  server.getJson("/fortnite/api/calendar/v1/timeline", getTimeline);
  server.getJson("/fortnite/api/game/v2/events/tournamentandhistory/:accountId/EU/WindowsClient", getTournamentHistory);
  server.get("/waitingroom/api/waitingroom", (context) => Response(statusCode: 204));
  server.postJson("/api/v1/user/setting", (context) => []);
  server.getJson("/eulatracking/api/public/agreements/fn/account/*", (context) => Response(statusCode: 204));
  server.getJson("/socialban/api/public/v1/:accountId", getSocialBan);
  server.getJson("/party/api/v1/Fortnite/user/*", getParty);
  server.getJson("/friends/api/v1/*/settings", (context) => {});
  server.getJson("/friends/api/v1/*/blocklist", (context) => {});
  server.getJson("/friends/api/public/friends", (context) => []);
  server.getJson("/friends/api/v1/:accountId/summary", (context) => []);
  server.getJson("/friends/api/public/list/fortnite/*/recentPlayers", (context) => []);
  server.getJson("/friends/api/public/blocklist/*", getBlockedFriends);

  // Privacy
  server.getJson("/fortnite/api/game/v2/privacy/account/:accountId", getPrivacy);
  server.postJson("/fortnite/api/game/v2/privacy/account/:accountId", postPrivacy);

  return server;
}
Jaguar _createMatchmaker(){
  var server = Jaguar(address: "127.0.0.1", port: 8080);
  WebSocket? ws;
  server.wsStream(
      "/",
      (_, input) => ws = input,
      after: [(_) => queueMatchmaking(ws!)]
  );
  return _addLoggingCapabilities(server);
}

Jaguar _addLoggingCapabilities(Jaguar server) {
  if(_loggingCapabilities){
    return server;
  }

  server.log.onRecord.listen((line) {
    stdout.writeln(line);
    serverLogFile.writeAsString("$line\n", mode: FileMode.append);
  });

  server.onException.add((ctx, exception, trace) {
    stderr.writeln("An error occurred: $exception");
    serverLogFile.writeAsString("An error occurred at ${ctx.uri}: \n$exception\n$trace\n", mode: FileMode.append);
  });

  _loggingCapabilities = true;
  return server;
}