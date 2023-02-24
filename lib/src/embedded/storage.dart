import 'package:path/path.dart' as path;
import 'dart:io';

import 'package:jaguar/jaguar.dart';
import 'package:jaguar/http/context/context.dart';

import 'package:crypto/crypto.dart';
import 'package:reboot_launcher/src/embedded/utils.dart';

import '../util/os.dart';

final Directory _settings = Directory("${Platform.environment["UserProfile"]}\\.reboot_launcher\\backend\\settings");

List getStorageSettings(Context context) =>
    loadEmbeddedDirectory("config")
        .listSync()
        .map((e) => File(e.path))
        .map(_getStorageSetting)
        .toList();

Map<String, Object> _getStorageSetting(File file){
  var name = path.basename(file.path);
  var bytes = file.readAsBytesSync();
  return {
    "uniqueFilename": name,
    "filename": name,
    "hash": sha1.convert(bytes).toString(),
    "hash256": sha256.convert(bytes).toString(),
    "length": bytes.length,
    "contentType": "application/octet-stream",
    "uploaded": "2020-02-23T18:35:53.967Z",
    "storageType": "S3",
    "storageIds": {},
    "doNotCache": true
  };
}

Response getStorageSetting(Context context) {
  var file = loadEmbedded("config\\${context.pathParams.get("file")}");
  return Response(body: file.readAsStringSync());
}

Response getStorageFile(Context context) {
  if (context.pathParams.get("file")?.toLowerCase() != "clientsettings.sav") {
    return Response.json(
        {"error": "File not found"},
        statusCode: 404
    );
  }

  var file = _getSettingsFile(context);
  return Response(
      body: file.existsSync() ? file.readAsBytesSync() : null,
      headers: {"content-type": "application/octet-stream"}
  );
}

List<Map<String, Object?>> getStorageAccount(Context context) {
  var file = _getSettingsFile(context);
  if (!file.existsSync()) {
    return [];
  }

  var content = file.readAsBytesSync();
  return [{
    "uniqueFilename": "ClientSettings.Sav",
    "filename": "ClientSettings.Sav",
    "hash": sha1.convert(content).toString(),
    "hash256": sha256.convert(content).toString(),
    "length": content.length,
    "contentType": "application/octet-stream",
    "uploaded": "2020-02-23T18:35:53.967Z",
    "storageType": "S3",
    "storageIds": {},
    "accountId": context.pathParams.get("accountId"),
    "doNotCache": true
  }];
}

Future<Response> addStorageFile(Context context) async {
  if(!_settings.existsSync()){
    await _settings.create(recursive: true);
  }

  var file = _getSettingsFile(context);
  await file.writeAsBytes(await context.body);
  return Response(statusCode: 204);
}

File _getSettingsFile(Context context) {
  if(!_settings.existsSync()){
    _settings.createSync(recursive: true);
  }

  return File("${_settings.path}\\ClientSettings.Sav");
}