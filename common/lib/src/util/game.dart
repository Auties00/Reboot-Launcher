import 'dart:collection';
import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';
import 'dart:typed_data';
import 'package:ffi/ffi.dart';
import 'package:reboot_common/common.dart';
import 'package:win32/win32.dart';
import 'package:path/path.dart' as path;

final DynamicLibrary _shell32 = DynamicLibrary.open('shell32.dll');
final SHGetPropertyStoreFromParsingName =
_shell32.lookupFunction<
    Int32 Function(Pointer<Utf16> pszPath, Pointer<Void> pbc, Uint32 flags,
        Pointer<GUID> riid, Pointer<Pointer<COMObject>> ppv),
    int Function(Pointer<Utf16> pszPath, Pointer<Void> pbc, int flags,
        Pointer<GUID> riid, Pointer<Pointer<COMObject>> ppv)>('SHGetPropertyStoreFromParsingName');

final Uint8List _originalHeadless = Uint8List.fromList([
  45, 0, 105, 0, 110, 0, 118, 0, 105, 0, 116, 0, 101, 0, 115, 0, 101, 0, 115, 0, 115, 0, 105, 0, 111, 0, 110, 0, 32, 0, 45, 0, 105, 0, 110, 0, 118, 0, 105, 0, 116, 0, 101, 0, 102, 0, 114, 0, 111, 0, 109, 0, 32, 0, 45, 0, 112, 0, 97, 0, 114, 0, 116, 0, 121, 0, 95, 0, 106, 0, 111, 0, 105, 0, 110, 0, 105, 0, 110, 0, 102, 0, 111, 0, 95, 0, 116, 0, 111, 0, 107, 0, 101, 0, 110, 0, 32, 0, 45, 0, 114, 0, 101, 0, 112, 0, 108, 0, 97, 0, 121, 0
]);

final Uint8List _patchedHeadless = Uint8List.fromList([
  45, 0, 108, 0, 111, 0, 103, 0, 32, 0, 45, 0, 110, 0, 111, 0, 115, 0, 112, 0, 108, 0, 97, 0, 115, 0, 104, 0, 32, 0, 45, 0, 110, 0, 111, 0, 115, 0, 111, 0, 117, 0, 110, 0, 100, 0, 32, 0, 45, 0, 110, 0, 117, 0, 108, 0, 108, 0, 114, 0, 104, 0, 105, 0, 32, 0, 45, 0, 117, 0, 115, 0, 101, 0, 111, 0, 108, 0, 100, 0, 105, 0, 116, 0, 101, 0, 109, 0, 99, 0, 97, 0, 114, 0, 100, 0, 115, 0, 32, 0, 32, 0, 32, 0, 32, 0, 32, 0, 32, 0, 32, 0
]);

// Not used right now
final Uint8List _originalMatchmaking = Uint8List.fromList([
  63, 0, 69, 0, 110, 0, 99, 0, 114, 0, 121, 0, 112, 0, 116, 0, 105, 0, 111, 0, 110, 0, 84, 0, 111, 0, 107, 0, 101, 0, 110, 0, 61
]);

final Uint8List _patchedMatchmaking = Uint8List.fromList([
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
]);

// https://github.com/polynite/fn-releases
const Map<int, String> _buildToGameVersion = {
  2870186: "1.0.0",
  3700114: "1.7.2",
  3724489: "1.8.0",
  3729133: "1.8.1",
  3741772: "1.8.2",
  3757339: "1.9",
  3775276: "1.9.1",
  3790078: "1.10",
  3807424: "1.11",
  3825894: "2.1",
  3841827: "2.2",
  3847564: "2.3",
  3858292: "2.4",
  3870737: "2.4.2",
  3889387: "2.5",
  3901517: "3.0.0",
  3915963: "3.1",
  3917250: "3.1.1",
  3935073: "3.2",
  3942182: "3.3",
  4008490: "3.5",
  4019403: "3.6",
  4039451: "4.0",
  4053532: "4.1",
  4072250: "4.2",
  4117433: "4.4",
  4127312: "4.4.1",
  4159770: "4.5",
  4204761: "5.0",
  4214610: "5.01",
  4240749: "5.10",
  4288479: "5.21",
  4305896: "5.30",
  4352937: "5.40",
  4363240: "5.41",
  4395664: "6.0",
  4424678: "6.01",
  4461277: "6.0.2",
  4464155: "6.10",
  4476098: "6.10.1",
  4480234: "6.10.2",
  4526925: "6.21",
  4543176: "6.22",
  4573279: "6.31",
  4629139: "7.0",
  4667333: "7.10",
  4727874: "7.20",
  4834550: "7.30",
  5046157: "7.40",
  5203069: "8.00",
  5625478: "8.20",
  5793395: "8.30",
  6005771: "8.40",
  6058028: "8.50",
  6165369: "8.51",
  6337466: "9.00",
  6428087: "9.01",
  6639283: "9.10",
  6922310: "9.21",
  7095426: "9.30",
  7315705: "9.40",
  7609292: "9.41",
  7704164: "10.00",
  7955722: "10.10",
  8456527: "10.20",
  8723043: "10.31",
  9380822: "10.40",
  9603448: "11.00",
  9901083: "11.10",
  10708866: "11.30",
  10800459: "11.31",
  11265652: "11.50",
  11556442: "12.00",
  11883027: "12.10",
  12353830: "12.21",
  12905909: "12.41",
  13137020: "12.50",
  13498980: "12.61",
  14113327: "13.40",
  14211474: "14.00",
  14456520: "14.30",
  14550713: "14.40",
  14786821: "14.60",
  14835335: "15.00",
  15014719: "15.10",
  15341163: "15.30",
  15526472: "15.50",
  15913292: "16.10",
  16163563: "16.30",
  16218553: "16.40",
  16469788: "16.50",
  16745144: "17.10",
  17004569: "17.30",
  17269705: "17.40",
  17388565: "17.50",
  17468642: "18.00",
  17661844: "18.10",
  17745267: "18.20",
  17811397: "18.21",
  17882303: "18.30",
  18163738: "18.40",
  18489740: "19.01",
  18675304: "19.10",
  19458861: "20.00",
  19598943: "20.10",
  19751212: "20.20",
  19950687: "20.30",
  20244966: "20.40",
  20463113: "21.00",
  20696680: "21.10",
  21035704: "21.20",
  21657658: "21.50",
};

Future<bool> patchHeadless(File file) async =>
    await _patch(file, _originalHeadless, _patchedHeadless);

Future<bool> patchMatchmaking(File file) async =>
    await _patch(file, _originalMatchmaking, _patchedMatchmaking);

Future<bool> _patch(File file, Uint8List original, Uint8List patched) async => Isolate.run(() async {
  try {
    if(original.length != patched.length){
      throw Exception("Cannot mutate length of binary file");
    }

    final source = await file.readAsBytes();
    var readOffset = 0;
    var patchOffset = -1;
    var patchCount = 0;
    while(readOffset < source.length){
      if(source[readOffset] == original[patchCount]){
        if(patchOffset == -1) {
          patchOffset = readOffset;
        }

        if(readOffset - patchOffset + 1 == original.length) {
          break;
        }

        patchCount++;
      }else {
        patchOffset = -1;
        patchCount = 0;
      }

      readOffset++;
    }

    if(patchOffset == -1) {
      return false;
    }

    for(var i = 0; i < patched.length; i++) {
      source[patchOffset + i] = patched[i];
    }

    await file.writeAsBytes(source, flush: true);
    return true;
  }catch(_){
    return false;
  }
});

List<String> createRebootArgs(String username, String password, bool host, bool headless, bool logging, String additionalArgs) {
  log("[PROCESS] Generating reboot args");
  if(password.isEmpty) {
    username = '${_parseUsername(username, host)}@projectreboot.dev';
  }

  password = password.isNotEmpty ? password : "Rebooted";
  final args = LinkedHashMap<String, String>(
      equals: (a, b) => a.toUpperCase() == b.toUpperCase(),
      hashCode: (a) => a.toUpperCase().hashCode
  );
  args.addAll({
    "-epicapp": "Fortnite",
    "-epicenv": "Prod",
    "-epiclocale": "en-us",
    "-epicportal": "",
    "-skippatchcheck": "",
    "-nobe": "",
    "-fromfl": "eac",
    "-fltoken": "3db3ba5dcbd2e16703f3978d",
    "-caldera": "eyJhbGciOiJFUzI1NiIsInR5cCI6IkpXVCJ9.eyJhY2NvdW50X2lkIjoiYmU5ZGE1YzJmYmVhNDQwN2IyZjQwZWJhYWQ4NTlhZDQiLCJnZW5lcmF0ZWQiOjE2Mzg3MTcyNzgsImNhbGRlcmFHdWlkIjoiMzgxMGI4NjMtMmE2NS00NDU3LTliNTgtNGRhYjNiNDgyYTg2IiwiYWNQcm92aWRlciI6IkVhc3lBbnRpQ2hlYXQiLCJub3RlcyI6IiIsImZhbGxiYWNrIjpmYWxzZX0.VAWQB67RTxhiWOxx7DBjnzDnXyyEnX7OljJm-j2d88G_WgwQ9wrE6lwMEHZHjBd1ISJdUO1UVUqkfLdU5nofBQ",
    "-AUTH_LOGIN": username,
    "-AUTH_PASSWORD": password.isNotEmpty ? password : "Rebooted",
    "-AUTH_TYPE": "epic"
  });

  if(logging) {
    args["-log"] = "";
  }

  if(host) {
    args["-nosplash"] = "";
    args["-nosound"] = "";
    if(headless){
      args["-nullrhi"] = "";
    }
  }

  log("[PROCESS] Default args: $args");
  log("[PROCESS] Adding custom args: $additionalArgs");
  for(final additionalArg in additionalArgs.split(" ")) {
    log("[PROCESS] Processing custom arg: $additionalArg");
    final separatorIndex = additionalArg.indexOf("=");
    final argName = separatorIndex == -1 ? additionalArg : additionalArg.substring(0, separatorIndex);
    log("[PROCESS] Custom arg key: $argName");
    final argValue = separatorIndex == -1 || separatorIndex + 1 >= additionalArg.length ? "" : additionalArg.substring(separatorIndex + 1);
    log("[PROCESS] Custom arg value: $argValue");
    args[argName] = argValue;
    log("[PROCESS] Updated args: $args");
  }

  log("[PROCESS] Final args result: $args");
  return args.entries
      .map((entry) => entry.value.isEmpty ? entry.key : "${entry.key}=${entry.value}")
      .toList();
}

void handleGameOutput({
  required String line,
  required bool host,
  required void Function() onLoggedIn,
  required void Function() onMatchEnd,
  required void Function() onShutdown,
  required void Function() onTokenError,
  required void Function() onBuildCorrupted,
}) {
  if (line.contains(kShutdownLine)) {
    log("[FORTNITE_OUTPUT_HANDLER] Detected shutdown: $line");
    onShutdown();
  }else if(kCorruptedBuildErrors.any((element) => line.contains(element))){
    log("[FORTNITE_OUTPUT_HANDLER] Detected corrupt build: $line");
    onBuildCorrupted();
  }else if(kCannotConnectErrors.any((element) => line.contains(element))){
    log("[FORTNITE_OUTPUT_HANDLER] Detected cannot connect error: $line");
    onTokenError();
  }else if(kLoggedInLines.every((entry) => line.contains(entry))) {
    log("[FORTNITE_OUTPUT_HANDLER] Detected logged in: $line");
    onLoggedIn();
  }else if(line.contains(kGameFinishedLine) && host) {
    log("[FORTNITE_OUTPUT_HANDLER] Detected match end: $line");
    onMatchEnd();
  }
}

String _parseUsername(String username, bool host) {
  if (username.isEmpty) {
    return kDefaultPlayerName;
  }

  username = username.replaceAll(RegExp("[^A-Za-z0-9]"), "").trim();
  if(username.isEmpty){
    return kDefaultPlayerName;
  }

  return username;
}

// Parsing the version is not that easy
// Also on some versions the shipping exe has it as well, but not on all: that's why i'm using the crash report client
//                ++Fortnite+Release-34.10-CL-40567068
// 4.16.0-3700114+++Fortnite+Release-Cert
// 4.19.0-3870737+++Fortnite+Release-Next
// 4.20.0-4008490+++Fortnite+Release-3.5
Future<String> extractGameVersion(Directory directory) => Isolate.run(() async {
  log("[VERSION] Looking for $kCrashReportExe in ${directory.path}");
  final defaultGameVersion = path.basename(directory.path);
  final crashReportClients = await findFiles(directory, kCrashReportExe);
  if (crashReportClients.isEmpty) {
    log("[VERSION] Didn't find a unique match: $crashReportClients");
    return defaultGameVersion;
  }

  log("[VERSION] Extracting game version from ${crashReportClients.last.path}(default: $defaultGameVersion)");
  final filePathPtr = crashReportClients.last.path.toNativeUtf16();
  final pPropertyStore = calloc<COMObject>();
  final iidPropertyStore = GUIDFromString(IID_IPropertyStore);
  final ret = SHGetPropertyStoreFromParsingName(
      filePathPtr,
      nullptr,
      GETPROPERTYSTOREFLAGS.GPS_DEFAULT,
      iidPropertyStore,
      pPropertyStore.cast()
  );

  calloc.free(filePathPtr);
  calloc.free(iidPropertyStore);

  if (FAILED(ret)) {
    log("[VERSION] Using default value");
    calloc.free(pPropertyStore);
    return defaultGameVersion;
  }

  final propertyStore = IPropertyStore(pPropertyStore);

  final countPtr = calloc<Uint32>();
  final hrCount = propertyStore.getCount(countPtr);
  final count = countPtr.value;
  calloc.free(countPtr);
  if (FAILED(hrCount)) {
    log("[VERSION] Using default value");
    return defaultGameVersion;
  }

  for (var i = 0; i < count; i++) {
    final pKey = calloc<PROPERTYKEY>();
    final hrKey = propertyStore.getAt(i, pKey);
    if (FAILED(hrKey)) {
      calloc.free(pKey);
      continue;
    }

    final pv = calloc<PROPVARIANT>();
    final hrValue = propertyStore.getValue(pKey, pv);
    if (!FAILED(hrValue)) {
      if (pv.ref.vt == VARENUM.VT_LPWSTR) {
        final valueStr = pv.ref.pwszVal.toDartString();
        final headerIndex = valueStr.indexOf("++Fortnite");
        if (headerIndex != -1) {
          log("[VERSION] Found value string: $valueStr");
          var gameVersion = valueStr.substring(valueStr.indexOf("-", headerIndex) + 1);
          log("[VERSION] Game version: $gameVersion");
          if(gameVersion == "Cert" || gameVersion == "Next") {
            final engineVersion = valueStr.substring(0, valueStr.indexOf("+"));
            log("[VERSION] Engine version: $engineVersion");
            final engineVersionParts = engineVersion.split("-");
            final engineVersionBuild = int.parse(engineVersionParts[1]);
            log("[VERSION] Engine build: $engineVersionBuild");
            gameVersion = _buildToGameVersion[engineVersionBuild] ?? defaultGameVersion;
          }
          log("[VERSION] Parsed game version: $gameVersion");
          return gameVersion;
        }
      }
    }
    calloc.free(pKey);
    calloc.free(pv);
  }

  log("[VERSION] Using default value");
  return defaultGameVersion;
});