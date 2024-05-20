import 'dart:io';

import 'package:args/args.dart';
import 'package:reboot_cli/src/game.dart';
import 'package:reboot_cli/src/reboot.dart';
import 'package:reboot_cli/src/server.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_common/src/util/matchmaker.dart' as matchmaker;

late String? username;
late bool host;
late bool verbose;
late String dll;
late FortniteVersion version;
late bool autoRestart;

void main(List<String> args) async {
  stdout.writeln("Reboot Launcher");
  stdout.writeln("Wrote by Auties00");
  stdout.writeln("Version 1.0");

  kill();

  var parser = ArgParser()
    ..addOption("path", mandatory: true)
    ..addOption("username")
    ..addOption("server-type", allowed: ServerType.values.map((entry) => entry.name), defaultsTo: ServerType.embedded.name)
    ..addOption("server-host")
    ..addOption("server-port")
    ..addOption("matchmaking-address")
    ..addOption("dll", defaultsTo: rebootDllFile.path)
    ..addFlag("update", defaultsTo: true, negatable: true)
    ..addFlag("log", defaultsTo: false)
    ..addFlag("host", defaultsTo: false)
    ..addFlag("auto-restart", defaultsTo: false, negatable: true);
  var result = parser.parse(args);

  dll = result["dll"];
  host = result["host"];
  username = result["username"] ?? kDefaultPlayerName;
  verbose = result["log"];
  version = FortniteVersion(name: "Dummy", location: Directory(result["path"]));

  await downloadRequiredDLLs();
  if(result["update"]) {
    stdout.writeln("Updating reboot dll...");
    try {
      await downloadRebootDll(kRebootDownloadUrl);
    }catch(error){
      stderr.writeln("Cannot update reboot dll: $error");
    }
  }

  stdout.writeln("Launching game...");
  var executable = await version.executable;
  if(executable == null){
    throw Exception("Missing game executable at: ${version.location.path}");
  }

  var started = await startServerCli(
      result["server-host"],
      result["server-port"],
      ServerType.values.firstWhere((element) => element.name == result["server-type"])
  );
  if(!started){
    stderr.writeln("Cannot start server!");
    return;
  }

  matchmaker.writeMatchmakingIp(result["matchmaking-address"]);
  autoRestart = result["auto-restart"];
  await startGame();
}