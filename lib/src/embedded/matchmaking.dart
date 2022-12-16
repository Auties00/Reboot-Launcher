import 'dart:convert';
import 'dart:io';

import 'package:crypto/crypto.dart';
import 'package:jaguar/http/context/context.dart';
import 'package:uuid/uuid.dart';
import 'package:jaguar/jaguar.dart';

String _build = "0";
String? _customIp;

Map<String, Object> getPlayerTicket(Context context){
  var bucketId = context.query.get("bucketId");
  if(bucketId == null){
    return {"Error": "Missing bucket id"};
  }

  _build = bucketId.split(":")[0];
  _customIp = context.query.get("player.option.customKey");
  return {
    "serviceUrl": "ws://127.0.0.1:8080",
    "ticketType": "mms-player",
    "payload": "69=",
    "signature": "420="
  };
}

Map<String, Object?> getSessionAccount(Context context) => {
    "accountId": context.pathParams.get("accountId"),
    "sessionId": context.pathParams.get("sessionId"),
    "key": "AOJEv8uTFmUh7XM2328kq9rlAzeQ5xzWzPIiyKn2s7s="
  };

Future<Map<String, Object?>> getMatch(Context context, String Function() ipQuery) async {
  var ipAndPort = _customIp ?? ipQuery().trim();
  var ip = ipAndPort.contains(":") ? ipAndPort.split(":")[0] : ipAndPort;
  var port = ipAndPort.contains(":") ? int.parse(ipAndPort.split(":")[1]) : 7777;
  return {
    "id": context.pathParams.get("sessionId"),
    "ownerId": _randomUUID(),
    "ownerName": "[DS]fortnite-liveeugcec1c2e30ubrcore0a-z8hj-1968",
    "serverName": "[DS]fortnite-liveeugcec1c2e30ubrcore0a-z8hj-1968",
    "serverAddress": ip,
    "serverPort": port,
    "maxPublicPlayers": 220,
    "openPublicPlayers": 175,
    "maxPrivatePlayers": 0,
    "openPrivatePlayers": 0,
    "attributes": {
      "REGION_s": "EU",
      "GAMEMODE_s": "FORTATHENA",
      "ALLOWBROADCASTING_b": true,
      "SUBREGION_s": "GB",
      "DCID_s": "FORTNITE-LIVEEUGCEC1C2E30UBRCORE0A-14840880",
      "tenant_s": "Fortnite",
      "MATCHMAKINGPOOL_s": "Any",
      "STORMSHIELDDEFENSETYPE_i": 0,
      "HOTFIXVERSION_i": 0,
      "PLAYLISTNAME_s": "Playlist_DefaultSolo",
      "SESSIONKEY_s": _randomUUID(),
      "TENANT_s": "Fortnite",
      "BEACONPORT_i": 15009
    },
    "publicPlayers": [],
    "privatePlayers": [],
    "totalPlayers": 45,
    "allowJoinInProgress": false,
    "shouldAdvertise": false,
    "isDedicated": false,
    "usesStats": false,
    "allowInvites": false,
    "usesPresence": false,
    "allowJoinViaPresence": true,
    "allowJoinViaPresenceFriendsOnly": false,
    "buildUniqueId": _build,
    "lastUpdated": "2022-11-08T18:55:52.341Z",
    "started": false
  };
}

List<Map<String, Object>> getMatchmakingRequests() => [];

void queueMatchmaking(WebSocket ws) {
  var now = DateTime.now();
  var ticketId = md5.convert(utf8.encode("1$now")).toString();
  var matchId = md5.convert(utf8.encode("2$now")).toString();
  var sessionId = md5.convert(utf8.encode("3$now")).toString();

  ws.addUtf8Text(utf8.encode(
      jsonEncode({
        "payload": {
          "state": "Connecting"
        },
        "name": "StatusUpdate"
      })
  ));

  ws.addUtf8Text(utf8.encode(
      jsonEncode({
        "payload": {
          "totalPlayers": 1,
          "connectedPlayers": 1,
          "state": "Waiting"
        },
        "name": "StatusUpdate"
      })
  ));

  ws.addUtf8Text(utf8.encode(
      jsonEncode({
        "payload": {
          "ticketId": ticketId,
          "queuedPlayers": 0,
          "estimatedWaitSec": 0,
          "status": {},
          "state": "Queued"
        },
        "name": "StatusUpdate"
      })
  ));

  ws.addUtf8Text(utf8.encode(
      jsonEncode({
        "payload": {
          "matchId": matchId,
          "state": "SessionAssignment"
        },
        "name": "StatusUpdate"
      })
  ));

  ws.addUtf8Text(utf8.encode(
      jsonEncode({
        "payload": {
          "matchId": matchId,
          "sessionId": sessionId,
          "joinDelaySec": 1
        },
        "name": "Play"
      })
  ));
}

String _randomUUID() => const Uuid().v4().replaceAll("-", "").toUpperCase();