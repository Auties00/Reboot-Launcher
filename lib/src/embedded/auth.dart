import 'dart:collection';
import 'dart:convert';
import 'dart:io';

import 'package:jaguar/http/context/context.dart';
import 'package:reboot_launcher/src/embedded/utils.dart';

import '../util/os.dart';

final Directory _profiles = Directory("${Platform.environment["UserProfile"]}\\.reboot_launcher\\backend\\profiles");

const String _token = "reboot_token";
const String _clientId = "reboot_client";
const String _device = "reboot_device";
const String _sessionId = "3c3662bcb661d6de679c636744c66b62";

List<Map<String, Object>> getAccounts(Context context) {
  return context.query.getList("accountId").map(getAccount).toList();
}

Map<String, Object> getAccount(String account) {
  return {"id": account, "displayName": _parseUsername(account), "externalAuths": {}};
}

Map<String, Object> getAccountInfo(Context context) {
  var usernameId = context.pathParams.get("accountId")!;
  var accountName = _parseUsername(usernameId);
  return {
    "id": usernameId,
    "displayName": accountName,
    "name": "Reboot",
    "email": usernameId,
    "failedLoginAttempts": 0,
    "lastLogin": "2022-11-08T18:55:52.341Z",
    "numberOfDisplayNameChanges": 0,
    "ageGroup": "UNKNOWN",
    "headless": false,
    "country": "US",
    "lastName": "Server",
    "preferredLanguage": "en",
    "canUpdateDisplayName": false,
    "tfaEnabled": false,
    "emailVerified": true,
    "minorVerified": false,
    "minorExpected": false,
    "minorStatus": "UNKNOWN"
  };
}

List<Map<String, Object>> getExternalAuths(Context context) => [];

Future<Map<String, Object>> getOAuthToken(Context context) async {
  var usernameId = await _getUsername(context);
  var accountName = _parseUsername(usernameId);
  return {
    "access_token": _token,
    "expires_in": 28800,
    "expires_at": "9999-12-02T01:12:01.100Z",
    "token_type": "bearer",
    "refresh_token": _token,
    "refresh_expires": 86400,
    "refresh_expires_at": "9999-12-02T01:12:01.100Z",
    "account_id": usernameId,
    "client_id": _clientId,
    "internal_client": true,
    "client_service": "fortnite",
    "displayName": accountName,
    "app": "fortnite",
    "in_app_id": usernameId,
    "device_id": _device
  };
}

Future<String> _getUsername(Context context) async {
  var params = await parseBody(context);
  var username = params["username"];
  return username ?? "unknown@projectreboot.dev";
}

Map<String, Object> verifyOAuthToken(Context context) {
  return {
    "token": _token,
    "session_id": _sessionId,
    "token_type": "bearer",
    "client_id": _clientId,
    "internal_client": true,
    "client_service": "fortnite",
    "account_id": "unknown",
    "expires_in": 28800,
    "expires_at": "9999-12-02T01:12:01.100Z",
    "auth_method": "exchange_code",
    "display_name": "unknown",
    "app": "fortnite",
    "in_app_id": "unknown",
    "device_id": _device
  };
}

List<Map<String, Object>> getExchange(Context context) => [];

List<String> getSsoDomains(Context context) => [
  "unrealengine.com",
  "unrealtournament.com",
  "fortnite.com",
  "epicgames.com"
];

String tryPlayOnPlatform(Context context) => "true";

List<Map<String, Object>> getFeatures(Context context) => [];

Map<String, Object?> getProfile(Context context){
  var profileId = context.query.get("profileId");
  if (profileId == null) {
    return {"Error": "Profile not defined."};
  }

  var profileJson = _getProfileJson(profileId, context);
  var profileFile = _getProfileFile(context);
  var baseRevision = profileJson["rvn"] ?? 0;
  var queryRevision = context.query.getInt("rvn") ?? -1;
  var profileChanges = _getFullProfileUpdate(context, profileId, profileJson, queryRevision, baseRevision);
  if(profileId == "athena" && !profileFile.existsSync()) {
    profileFile.writeAsStringSync(json.encode(profileJson), flush: true);
  }

  return {
    "profileRevision": baseRevision,
    "profileId": profileId,
    "profileChangesBaseRevision": baseRevision,
    "profileChanges": profileChanges,
    "profileCommandRevision": profileJson["commandRevision"] ?? 0,
    "serverTime": "2022-11-08T18:55:52.341Z",
    "responseVersion": 1
  };
}

Map<String, dynamic> _getProfileJson(String profileId, Context context) {
  if(profileId == "athena"){
    var profile = _getProfileFile(context);
    if(profile.existsSync()){
      return json.decode(profile.readAsStringSync());
    }

    var body = loadEmbedded("profiles/$profileId.json").readAsStringSync();
    return json.decode(body);
  }

  var profileJson = json.decode(loadEmbedded("profiles/$profileId.json").readAsStringSync());
  return profileJson;
}

Future<Map<String, Object>> equipItem(Context context) async {
  var profileFile = _getProfileFile(context);
  var profileJson = json.decode(profileFile.readAsStringSync());
  var baseRevision = profileJson["rvn"] ?? 0;
  var queryRevision = context.query.getInt("rvn") ?? -1;

  var body = json.decode(utf8.decode(await context.body));
  var variant = _getReturnVariant(body, profileJson);
  var change = _getStatsChanged(body, profileJson);
  var profileChanges = _getProfileChanges(queryRevision, baseRevision, profileJson, change, body, variant);
  profileFile.writeAsStringSync(json.encode(profileJson));
  return {
    "profileRevision": baseRevision,
    "profileId": "athena",
    "profileChangesBaseRevision": baseRevision,
    "profileChanges": profileChanges,
    "profileCommandRevision": profileJson["commandRevision"] ?? 0,
    "serverTime": "2022-11-08T18:55:52.341Z",
    "responseVersion": 1
  };
}

List<dynamic> _getProfileChanges(int queryRevision, baseRevision, profileJson, bool change, body, bool variant) {
  var changes = [];
  if (change) {
    var category = ("favorite_${body["slotName"] ?? "character"}")
        .toLowerCase();
    if (category == "favorite_itemwrap") {
      category += "s";
    }

    profileJson["rvn"] = (profileJson["rvn"] ?? 0) + 1;
    profileJson["commandRevision"] = (profileJson["commandRevision"] ?? 0) + 1;

    changes.add({
      "changeType": "statModified",
      "name": category,
      "value": profileJson["stats"]["attributes"][category]
    });
    if (variant) {
      changes.add({
        "changeType": "itemAttrChanged",
        "itemId": body["itemToSlot"],
        "attributeName": "variants",
        "attributeValue": profileJson["items"][body["itemToSlot"]]["attributes"]["variants"]
      });
    }
  }

  if(queryRevision != baseRevision){
    return [{
      "changeType": "fullProfileUpdate",
      "profile": profileJson
    }];
  }

  return changes;
}

bool _getStatsChanged(body, profileJson) {
  var slotName = body["slotName"];
  if (slotName == null) {
    return false;
  }

  switch (slotName) {
    case "Character":
      profileJson["stats"]["attributes"]["favorite_character"] =
          body["itemToSlot"] ?? "";
      return true;
    case "Backpack":
      profileJson["stats"]["attributes"]["favorite_backpack"] =
          body["itemToSlot"] ?? "";
      return true;
    case "Pickaxe":
      profileJson["stats"]["attributes"]["favorite_pickaxe"] =
          body["itemToSlot"] ?? "";
      return true;
    case "Glider":
      profileJson["stats"]["attributes"]["favorite_glider"] =
          body["itemToSlot"] ?? "";
      return true;
    case "SkyDiveContrail":
      profileJson["stats"]["attributes"]["favorite_skydivecontrail"] =
          body["itemToSlot"] ?? "";
      return true;
    case "MusicPack":
      profileJson["stats"]["attributes"]["favorite_musicpack"] =
          body["itemToSlot"] ?? "";
      return true;
    case "LoadingScreen":
      profileJson["stats"]["attributes"]["favorite_loadingscreen"] =
          body["itemToSlot"] ?? "";
      return true;
    case "Dance":
      var index = body["indexWithinSlot"] ?? 0;
      if (index >= 0) {
        profileJson["stats"]["attributes"]["favorite_dance"][index] =
            body["itemToSlot"] ?? "";
      }

      return true;
    case "ItemWrap":
      var index = body["indexWithinSlot"] ?? 0;
      if (index < 0) {
        for (var i = 0; i < 7; i++) {
          profileJson["stats"]["attributes"]["favorite_itemwraps"][i] =
              body["itemToSlot"] ?? "";
        }
      } else {
        profileJson["stats"]["attributes"]["favorite_itemwraps"][index] =
            body["itemToSlot"] ?? "";
      }

      return true;
    default:
      return false;
  }
}

bool _getReturnVariant(body, profileJson) {
  var variantUpdates = body["variantUpdates"] ?? [];
  if(!variantUpdates.toString().contains("active")){
    return false;
  }

  try {
    var variantJson = profileJson["items"][body["itemToSlot"]]["attributes"]["variants"] ?? [];
    if (variantJson.isEmpty) {
      variantJson = variantUpdates;
    }

    for (var i in variantJson) {
      try {
        if (variantJson[i]["channel"].toLowerCase() == body["variantUpdates"][i]["channel"].toLowerCase()) {
          profileJson["items"][body["itemToSlot"]]["attributes"]["variants"][i]["active"] = body["variantUpdates"][i]["active"] ?? "";
        }
      } catch (_) {
        // Ignored
      }
    }

    return true;
  } catch (_) {
    // Ignored
  }

  return false;
}

List<Map<String, Object?>> _getFullProfileUpdate(Context context, String profileName, Map<String, dynamic> profileJson, int queryRevision, int baseRevision) {
  if (queryRevision == baseRevision) {
    return [];
  }

  if (profileName == "athena") {
    var season = parseSeason(context);
    profileJson["stats"]["attributes"]["season_num"] = season;
    profileJson["stats"]["attributes"]["book_purchased"] = true;
    profileJson["stats"]["attributes"]["book_level"] = 100;
    profileJson["stats"]["attributes"]["season_match_boost"] = 100;
    profileJson["stats"]["attributes"]["season_friend_match_boost"] = 100;
  }

  return [{
    "changeType": "fullProfileUpdate",
    "profile": profileJson
  }];
}

String _parseUsername(String username) =>
    username.contains("@") ? username.split("@")[0] : username;

File _getProfileFile(Context context) {
  if(!_profiles.existsSync()){
    _profiles.createSync(recursive: true);
  }

  return File("${_profiles.path}\\ClientProfile.json");
}

