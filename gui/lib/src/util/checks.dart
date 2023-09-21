import 'dart:io';

import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/util/translations.dart';

String? checkVersion(String? text, List<FortniteVersion> versions) {
  if (text == null || text.isEmpty) {
    return translations.emptyVersionName;
  }

  if (versions.any((element) => element.name == text)) {
    return translations.versionAlreadyExists;
  }

  return null;
}

String? checkChangeVersion(String? text) {
  if (text == null || text.isEmpty) {
    return translations.emptyVersionName;
  }

  return null;
}

String? checkGameFolder(text) {
  if (text == null || text.isEmpty) {
    return translations.emptyGamePath;
  }

  var directory = Directory(text);
  if (!directory.existsSync()) {
    return translations.directoryDoesNotExist;
  }

  if (FortniteVersionExtension.findExecutable(directory, "FortniteClient-Win64-Shipping.exe") == null) {
    return translations.missingShippingExe;
  }

  return null;
}

String? checkDownloadDestination(text) {
  if (text == null || text.isEmpty) {
    return translations.invalidDownloadPath;
  }

  return null;
}

String? checkDll(String? text) {
  if (text == null || text.isEmpty) {
    return translations.invalidDllPath;
  }

  if (!File(text).existsSync()) {
    return translations.dllDoesNotExist;
  }

  if (!text.endsWith(".dll")) {
    return translations.invalidDllExtension;
  }

  return null;
}

String? checkMatchmaking(String? text) {
  if (text == null || text.isEmpty) {
    return translations.emptyHostname;
  }

  var ipParts = text.split(":");
  if(ipParts.length > 2){
    return translations.hostnameFormat;
  }

  return null;
}

String? checkUpdateUrl(String? text) {
  if (text == null || text.isEmpty) {
    return translations.emptyURL;
  }

  return null;
}