import 'dart:io';

import '../model/fortnite_version.dart';

String? checkVersion(String? text, List<FortniteVersion> versions) {
  if (text == null || text.isEmpty) {
    return 'Empty version name';
  }

  if (versions.any((element) => element.name == text)) {
    return 'This version already exists';
  }

  return null;
}

String? checkChangeVersion(String? text) {
  if (text == null || text.isEmpty) {
    return 'Empty version name';
  }

  return null;
}

String? checkGameFolder(text) {
  if (text == null || text.isEmpty) {
    return 'Empty game path';
  }

  var directory = Directory(text);
  if (!directory.existsSync()) {
    return "Directory doesn't exist";
  }

  if (FortniteVersion.findExecutable(directory, "FortniteClient-Win64-Shipping.exe") == null) {
    return "Invalid game path";
  }

  return null;
}

String? checkDownloadDestination(text) {
  if (text == null || text.isEmpty) {
    return 'Invalid download path';
  }

  return null;
}

String? checkDll(String? text) {
  if (text == null || text.isEmpty) {
    return "Empty dll path";
  }

  if (!File(text).existsSync()) {
    return "This dll doesn't exist";
  }

  if (!text.endsWith(".dll")) {
    return "This file is not a dll";
  }

  return null;
}

String? checkMatchmaking(String? text) {
  if (text == null || text.isEmpty) {
    return "Empty hostname";
  }

  return null;
}