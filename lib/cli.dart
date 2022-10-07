import 'dart:collection';
import 'dart:convert';
import 'dart:io';

import 'package:args/args.dart';
import 'package:process_run/shell.dart';
import 'package:reboot_launcher/src/model/fortnite_version.dart';
import 'package:reboot_launcher/src/model/game_type.dart';
import 'package:reboot_launcher/src/util/binary.dart';
import 'package:reboot_launcher/src/util/injector.dart';
import 'package:reboot_launcher/src/util/patcher.dart';
import 'package:reboot_launcher/src/util/reboot.dart';
import 'package:reboot_launcher/src/util/server_standalone.dart';
import 'package:shelf_proxy/shelf_proxy.dart';
import 'package:win32_suspend_process/win32_suspend_process.dart';
import 'dart:ffi';

import 'package:ffi/ffi.dart';
import 'package:win32/win32.dart';
import 'package:shelf/shelf_io.dart' as shelf_io;
import 'package:http/http.dart' as http;

// Needed because binaries can't be loaded in any other way
const String _craniumDownload = "https://cdn.discordapp.com/attachments/1001161930599317524/1027684488718860309/cranium.dll";
const String _consoleDownload = "https://cdn.discordapp.com/attachments/1001161930599317524/1027684489184432188/console.dll";
const String _injectorDownload = "https://cdn.discordapp.com/attachments/1001161930599317524/1027686593697435799/injector.exe";

Process? _gameProcess;

void main(List<String> args){
  handleCLI(args);
}

Future<Map<String, dynamic>> _getControllerJson(String name) async {
  var folder = await _getWindowsPath(FOLDERID_Documents);
  if(folder == null){
    throw Exception("Missing documents folder");
  }

  var file = File("$folder/$name.gs");
  if(!file.existsSync()){
    return HashMap();
  }

  return jsonDecode(file.readAsStringSync());
}

Future<String?> _getWindowsPath(String folderID) {
  final Pointer<Pointer<Utf16>> pathPtrPtr = calloc<Pointer<Utf16>>();
  final Pointer<GUID> knownFolderID = calloc<GUID>()..ref.setGUID(folderID);

  try {
    final int hr = SHGetKnownFolderPath(
      knownFolderID,
      KF_FLAG_DEFAULT,
      NULL,
      pathPtrPtr,
    );

    if (FAILED(hr)) {
      if (hr == E_INVALIDARG || hr == E_FAIL) {
        throw WindowsException(hr);
      }
      return Future<String?>.value();
    }

    final String path = pathPtrPtr.value.toDartString();
    return Future<String>.value(path);
  } finally {
    calloc.free(pathPtrPtr);
    calloc.free(knownFolderID);
  }
}

Future<void> handleCLI(List<String> args) async {
  stdout.writeln("Reboot Launcher CLI Tool");
  stdout.writeln("Wrote by Auties00");
  stdout.writeln("Version 3.11");

  var gameJson = await _getControllerJson("game");
  var serverJson = await _getControllerJson("server");
  var settingsJson = await _getControllerJson("settings");
  var versions = _getVersions(gameJson);
  var parser = ArgParser()
    ..addCommand("list")
    ..addCommand("launch")
    ..addOption("version", defaultsTo: gameJson["version"])
    ..addOption("username")
    ..addOption("server-type", allowed: ["embedded", "remote"], defaultsTo: serverJson["embedded"] ?? true ? "embedded" : "remote")
    ..addOption("server-host", defaultsTo: serverJson["host"])
    ..addOption("server-port", defaultsTo: serverJson["port"])
    ..addOption("dll", defaultsTo: settingsJson["reboot"] ?? (await loadBinary("reboot.dll", true)).path)
    ..addOption("type", allowed: ["client", "server", "headless_server"], defaultsTo: _getDefaultType(gameJson))
    ..addFlag("update", defaultsTo: settingsJson["auto_update"] ?? true, negatable: true)
    ..addFlag("log", defaultsTo: false);
  var result = parser.parse(args);
  if (result.command?.name == "list") {
    stdout.writeln("Versions list: ");
    versions.map((entry) => "${entry.location.path}(${entry.name})")
        .forEach((element) => stdout.writeln(element));
    return;
  }

  var type = _getType(result);
  var username = result["username"];
  username ??= gameJson["${type == GameType.client ? "game" : "server"}_username"];

  var dummyVersion = _createVersion(gameJson["version"], result["version"], versions);
  await _updateDLLs();
  if(result["update"]) {
    stdout.writeln("Updating reboot dll...");
    await downloadRebootDll(0);
  }

  stdout.writeln("Launching game(type: ${type.name})...");
  await _startLauncherProcess(dummyVersion);
  await _startEacProcess(dummyVersion);
  if (result["type"] == "headless_server") {
    await patchExe(dummyVersion.executable!);
  }

  var started = await _startServerIfNeeded(result);
  if(!started){
    stderr.writeln("Cannot start server!");
    return;
  }

  await _startGameProcess(dummyVersion, result["dll"], type != GameType.client, result);
  await _injectOrShowError("cranium.dll");
}

GameType _getType(ArgResults result) {
  var type = result["type"];
  switch(type){
    case "client":
      return GameType.client;

    case "server":
      return GameType.server;

    case "headless_server":
      return GameType.headlessServer;

    default:
      throw Exception("Unknown game type: $result. Use --type only with client, server or headless_server");
  }
}

String _getDefaultType(Map<String, dynamic> json){
  var type = GameType.values.elementAt(json["type"] ?? 0);
  switch(type){
    case GameType.client:
      return "client";
    case GameType.server:
      return "server";
    case GameType.headlessServer:
      return "headless_server";
  }
}

Future<void> _updateDLLs() async {
  stdout.writeln("Downloading necessary components...");
  var consoleDll = await loadBinary("console.dll", true);
  if(!consoleDll.existsSync()){
    var response = await http.get(Uri.parse(_consoleDownload));
    if(response.statusCode != 200){
      throw Exception("Cannot download console.dll");
    }

    await consoleDll.writeAsBytes(response.bodyBytes);
  }

  var craniumDll = await loadBinary("cranium.dll", true);
  if(!craniumDll.existsSync()){
    var response = await http.get(Uri.parse(_craniumDownload));
    if(response.statusCode != 200){
      throw Exception("Cannot download cranium.dll");
    }

    await craniumDll.writeAsBytes(response.bodyBytes);
  }

  var injectorExe = await loadBinary("injector.exe", true);
  if(!injectorExe.existsSync()){
    var response = await http.get(Uri.parse(_injectorDownload));
    if(response.statusCode != 200){
      throw Exception("Cannot download injector");
    }

    await injectorExe.writeAsBytes(response.bodyBytes);
  }
}

List<FortniteVersion> _getVersions(Map<String, dynamic> gameJson) {
  Iterable iterable = jsonDecode(gameJson["versions"] ?? "[]");
  return iterable.map((entry) => FortniteVersion.fromJson(entry))
      .toList();
}

Future<void> _startGameProcess(FortniteVersion dummyVersion, String rebootDll, bool host, ArgResults result) async {
  var gamePath = dummyVersion.executable?.path;
  if (gamePath == null) {
    throw Exception("${dummyVersion.location
        .path} no longer contains a Fortnite executable. Did you delete it?");
  }

  var username = result["username"];
  if (username == null) {
    username = "Reboot${host ? 'Host' : 'Player'}";
    stdout.writeln("No username was specified, using $username by default. Use --username to specify one");
  }

  var verbose = result["log"];
  _gameProcess = await Process.start(gamePath, createRebootArgs(username, result["type"] == "headless_server"))
    ..exitCode.then((_) => _onClose())
    ..outLines.forEach((line) => _onGameOutput(line, rebootDll, host, verbose));
}

void _onClose() {
  stdout.writeln("The game was closed");
  exit(0);
}

Future<void> _startEacProcess(FortniteVersion dummyVersion) async {
  if (dummyVersion.eacExecutable == null) {
    return;
  }

  var process = await Process.start(dummyVersion.eacExecutable!.path, []);
  Win32Process(process.pid).suspend();
}

Future<void> _startLauncherProcess(FortniteVersion dummyVersion) async {
  if (dummyVersion.launcher == null) {
    return;
  }

  var process = await Process.start(dummyVersion.launcher!.path, []);
  Win32Process(process.pid).suspend();
}

Future<bool> _startServerIfNeeded(ArgResults result) async {
  stdout.writeln("Starting lawin server...");
  if (!await isLawinPortFree()) {
    stdout.writeln("A lawin server is already active");
    return true;
  }

  if (result["server-type"] == "embedded") {
    stdout.writeln("Starting an embedded server...");
    return  await _changeEmbeddedServerState();
  }

  var host = result["server-host"];
  var port = result["server-port"];
  stdout.writeln("Starting a reverse proxy to $host:$port");
  return await _changeReverseProxyState(host, port) != null;
}

Future<bool> _changeEmbeddedServerState() async {
  var nodeProcess = await Process.run("where", ["node"]);
  if(nodeProcess.exitCode != 0) {
    throw Exception("Missing node, cannot start embedded server");
  }

  if(!serverLocation.existsSync()) {
    await downloadServer(false);
  }

  var serverRunner = File("${serverLocation.path}/start.bat");
  if (!(await serverRunner.exists())) {
    return false;
  }

  var nodeModules = Directory("${serverLocation.path}/node_modules");
  if (!(await nodeModules.exists())) {
    await Process.run("${serverLocation.path}/install_packages.bat", [],
        workingDirectory: serverLocation.path);
  }

  await Process.start(serverRunner.path, [],  workingDirectory: serverLocation.path);
  return true;
}

Future<HttpServer?> _changeReverseProxyState(String host, String port) async {
  host = host.trim();
  if(host.isEmpty){
    throw Exception("Missing host name");
  }

  port = port.trim();
  if(port.isEmpty){
    throw Exception("Missing port");
  }

  if(int.tryParse(port) == null){
    throw Exception("Invalid port, use only numbers");
  }

  try{
    var uri = await ping(host, port);
    if(uri == null){
      return null;
    }

    return await shelf_io.serve(proxyHandler(uri), "127.0.0.1", 3551);
  }catch(error){
    throw Exception("Cannot start reverse proxy");
  }
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

void _onGameOutput(String line, String rebootDll, bool host, bool verbose) {
  if(verbose) {
    stdout.writeln(line);
  }

  if (line.contains("FOnlineSubsystemGoogleCommon::Shutdown()")) {
    return;
  }

  if(line.contains("port 3551 failed: Connection refused")){
    stderr.writeln("Connection refused from lawin server");
    return;
  }

  if(line.contains("HTTP 400 response from ")){
    stderr.writeln("Connection refused from lawin server");
    return;
  }

  if(line.contains("Network failure when attempting to check platform restrictions")){
    stderr.writeln("Expired token, please reopen the game");
    return;
  }

  if (line.contains("Game Engine Initialized") && !host) {
    _injectOrShowError("console.dll");
    return;
  }

  if(line.contains("Region") && host){
    _injectOrShowError(rebootDll, false);
  }
}

Future<void> _injectOrShowError(String binary, [bool locate = true]) async {
  if (_gameProcess == null) {
    return;
  }

  try {
    stdout.writeln("Injecting $binary...");
    var dll = locate ? await loadBinary(binary, true) : File(binary);
    if(!dll.existsSync()){
      throw Exception("Cannot inject $dll: missing file");
    }

    var success = await injectDll(_gameProcess!.pid, dll.path, true);
    if (success) {
      return;
    }

    _onInjectError(binary);
  } catch (exception) {
    _onInjectError(binary);
  }
}

void _onInjectError(String binary) {
  stderr.writeln(injectLogFile.readAsStringSync());
  throw Exception("Cannot inject binary: $binary");
}