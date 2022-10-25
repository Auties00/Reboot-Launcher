import 'dart:io';

import 'package:args/args.dart';
import 'package:reboot_launcher/src/cli/compatibility.dart';
import 'package:reboot_launcher/src/cli/config.dart';
import 'package:reboot_launcher/src/cli/game.dart';
import 'package:reboot_launcher/src/cli/reboot.dart';
import 'package:reboot_launcher/src/cli/server.dart';
import 'package:reboot_launcher/src/model/fortnite_version.dart';
import 'package:reboot_launcher/src/model/game_type.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/util/patcher.dart';
import 'package:reboot_launcher/src/util/reboot.dart';


void main(List<String> args){
  handleCLI(args);
}

Future<void> handleCLI(List<String> args) async {
  stdout.writeln("Reboot Launcher");
  stdout.writeln("Wrote by Auties00");
  stdout.writeln("Version 4.4");

  kill();

  var gameJson = await getControllerJson("game");
  var serverJson = await getControllerJson("server");
  var settingsJson = await getControllerJson("settings");
  var versions = getVersions(gameJson);
  var parser = ArgParser()
    ..addCommand("list")
    ..addCommand("launch")
    ..addOption("version", defaultsTo: gameJson["version"])
    ..addOption("username")
    ..addOption("server-type", allowed: getServerTypes(), defaultsTo: getDefaultServerType(serverJson))
    ..addOption("server-host")
    ..addOption("server-port")
    ..addOption("dll", defaultsTo: settingsJson["reboot"] ?? (await loadBinary("reboot.dll", true)).path)
    ..addOption("type", allowed: getGameTypes(), defaultsTo: getDefaultGameType(gameJson))
    ..addFlag("update", defaultsTo: settingsJson["auto_update"] ?? true, negatable: true)
    ..addFlag("log", defaultsTo: false)
    ..addFlag("memory-fix", defaultsTo: false, negatable: true);
  var result = parser.parse(args);
  if (result.command?.name == "list") {
    stdout.writeln("Versions list: ");
    versions.map((entry) => "${entry.location.path}(${entry.name})")
        .forEach((element) => stdout.writeln(element));
    return;
  }

  var dll = result["dll"];
  var type = getGameType(result);
  var username = result["username"];
  username ??= gameJson["${type == GameType.client ? "game" : "server"}_username"];
  var verbose = result["log"];

  var dummyVersion = _createVersion(gameJson["version"], result["version"], result["memory-fix"], versions);
  await downloadRequiredDLLs();
  if(result["update"]) {
    stdout.writeln("Updating reboot dll...");
    await downloadRebootDll(0);
  }

  stdout.writeln("Launching game(type: ${type.name})...");
  if(dummyVersion.executable == null){
    throw Exception("Missing game executable at: ${dummyVersion.location.path}");
  }

  if (result["type"] == "headless_server") {
    await patchHeadless(dummyVersion.executable!);
  }else if(result["type"] == "client"){
    await patchMatchmaking(dummyVersion.executable!);
  }

  var serverType = getServerType(result);
  var host = result["server-host"] ?? serverJson["${serverType.id}_host"];
  var port = result["server-port"] ?? serverJson["${serverType.id}_port"];
  var started = await startServer(host, port, serverType);
  if(!started){
    stderr.writeln("Cannot start server!");
    return;
  }

  await startGame(username, type, verbose, dll, dummyVersion);
}

FortniteVersion _createVersion(String? versionName, String? versionPath, bool memoryFix, List<FortniteVersion> versions) {
  if (versionPath != null) {
    return FortniteVersion(name: "dummy", location: Directory(versionPath), memoryFix: memoryFix);
  }

  if(versionName != null){
    try {
      return versions.firstWhere((element) => versionName == element.name);
    }catch(_){
      throw Exception("Cannot find version $versionName");
    }
  }

  throw Exception(
      "Specify a version using --version or open the launcher GUI and select it manually");
}