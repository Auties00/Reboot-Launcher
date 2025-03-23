import 'dart:collection';
import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';
import 'dart:typed_data';
import 'package:ffi/ffi.dart';
import 'package:reboot_common/common.dart';
import 'package:win32/win32.dart';

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

Future<String> extractGameVersion(String filePath, String defaultGameVersion) => Isolate.run(() {
  final filePathPtr = filePath.toNativeUtf16();
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
    calloc.free(pPropertyStore);
    throw WindowsException(ret);
  }

  final propertyStore = IPropertyStore(pPropertyStore);

  final countPtr = calloc<Uint32>();
  final hrCount = propertyStore.getCount(countPtr);
  final count = countPtr.value;
  calloc.free(countPtr);
  if (FAILED(hrCount)) {
    throw WindowsException(hrCount);
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
        if (valueStr.contains("+++Fortnite")) {
          var gameVersion = valueStr.substring(valueStr.lastIndexOf("-") + 1);
          if(gameVersion == "Cert") {
            final engineVersion = valueStr.substring(0, valueStr.indexOf("+"));
            final engineVersionParts = engineVersion.split("-");
            final engineVersionBuild = int.parse(engineVersionParts[1]);
            switch (engineVersionBuild) {
              case 2870186:
                gameVersion = "OT6.5";
                break;
              case 3700114:
                gameVersion = "1.7.2";
                break;
              case 3724489:
                gameVersion = "1.8.0";
                break;
              case 3729133:
                gameVersion = "1.8.1";
                break;
              case 3741772:
                gameVersion = "1.8.2";
                break;
              case 3757339:
                gameVersion = "1.9";
                break;
              case 3775276:
                gameVersion = "1.9.1";
                break;
              case 3790078:
                gameVersion = "1.10";
                break;
              case 3807424:
                gameVersion = "1.11";
                break;
              case 3825894:
                gameVersion = "2.1";
                break;
              default:
                gameVersion = defaultGameVersion;
                break;
            }
          }
          return gameVersion;
        }
      }
    }
    calloc.free(pKey);
    calloc.free(pv);
  }

  return defaultGameVersion;
});