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

late String? username;
late GameType type;
late bool verbose;
late String dll;
late FortniteVersion version;
late bool autoRestart;

void main(List<String> args){
  handleCLI(args);
}

Future<void> handleCLI(List<String> args) async {
  stdout.writeln("Reboot Launcher");
  stdout.writeln("Wrote by Auties00");
  stdout.writeln("Version 5.3");

  kill();

  var gameJson = await getControllerJson("game");
  var serverJson = await getControllerJson("server");
  var settingsJson = await getControllerJson("settings");
  var versions = getVersions(gameJson);
  var parser = ArgParser()
    ..addCommand("list")
    ..addCommand("launch")
    ..addOption("version")
    ..addOption("username")
    ..addOption("server-type", allowed: getServerTypes(), defaultsTo: getDefaultServerType(serverJson))
    ..addOption("server-host")
    ..addOption("server-port")
    ..addOption("matchmaking-address")
    ..addOption("dll", defaultsTo: settingsJson["reboot"] ?? (await loadBinary("reboot.dll", true)).path)
    ..addOption("type", allowed: getGameTypes(), defaultsTo: getDefaultGameType(gameJson))
    ..addFlag("update", defaultsTo: settingsJson["auto_update"] ?? true, negatable: true)
    ..addFlag("log", defaultsTo: false)
    ..addFlag("auto-restart", defaultsTo: false, negatable: true);
  var result = parser.parse(args);
  if (result.command?.name == "list") {
    stdout.writeln("Versions list: ");
    versions.map((entry) => "${entry.location.path}(${entry.name})")
        .forEach((element) => stdout.writeln(element));
    return;
  }

  dll = result["dll"];
  type = getGameType(result);
  username = result["username"] ?? gameJson["${type == GameType.client ? "game" : "server"}_username"];
  verbose = result["log"];

  version = _createVersion(gameJson["version"], result["version"], versions);
  await downloadRequiredDLLs();
  if(result["update"]) {
    stdout.writeln("Updating reboot dll...");
    try {
      await downloadRebootDll(0);
    }catch(error){
      stderr.writeln("Cannot update reboot dll: $error");
    }
  }

  stdout.writeln("Launching game(type: ${type.name})...");
  if(version.executable == null){
    throw Exception("Missing game executable at: ${version.location.path}");
  }

  await patchHeadless(version.executable!);
  await patchMatchmaking(version.executable!);

  var serverType = getServerType(result);
  var host = result["server-host"] ?? serverJson["${serverType.id}_host"];
  var port = result["server-port"] ?? serverJson["${serverType.id}_port"];
  var started = await startServer(host, port, serverType, result["matchmaking-address"]);
  if(!started){
    stderr.writeln("Cannot start server!");
    return;
  }

  autoRestart = result["auto-restart"];
  await startGame();
}

FortniteVersion _createVersion(String? versionName, String? versionPath, List<FortniteVersion> versions) {
  if (versionPath != null) {
    return FortniteVersion(name: "dummy", location: Directory(versionPath));
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