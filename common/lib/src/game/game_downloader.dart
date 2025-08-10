import 'dart:io';

import 'package:archive/archive_io.dart';
import 'package:http/http.dart' as http;
import 'package:path/path.dart' as path;
import 'package:reboot_common/common.dart';
import 'dart:async';
import 'dart:convert';
import 'dart:isolate';

import 'package:uuid/uuid.dart';


final File rebootBeforeS20DllFile = File("${dllsDirectory.path}\\reboot.dll");
final File rebootAboveS20DllFile = File("${dllsDirectory.path}\\rebootS20.dll");

const String kRebootBelowS20DownloadUrl =
    "https://nightly.link/Milxnor/Project-Reboot-3.0/workflows/msbuild/master/Reboot.zip";
const String kRebootAboveS20DownloadUrl =
    "https://nightly.link/Milxnor/Project-Reboot-3.0/workflows/msbuild/master/RebootS20.zip";

const String _kRebootBelowS20FallbackDownloadUrl =
    "https://github.com/Auties00/reboot_launcher/raw/master/gui/dependencies/dlls/RebootFallback.zip";
const String _kRebootAboveS20FallbackDownloadUrl =
    "https://github.com/Auties00/reboot_launcher/raw/master/gui/dependencies/dlls/RebootS20Fallback.zip";

const String kStopBuildDownloadSignal = "kill";

final int _ariaPort = 6800;
final Uri _ariaEndpoint = Uri.parse('http://localhost:$_ariaPort/jsonrpc');
final Duration _ariaMaxSpawnTime = const Duration(seconds: 10);
final RegExp _rarProgressRegex = RegExp("^((100)|(\\d{1,2}(.\\d*)?))%\$");
final List<GameBuild> downloadableBuilds = [
    GameBuild(gameVersion: "1.7.2", link: "https://builds.rebootfn.org/1.7.2.zip", available: true),
    GameBuild(gameVersion: "1.8", link: "https://builds.rebootfn.org/1.8.rar", available: true),
    GameBuild(gameVersion: "1.8.1", link: "https://builds.rebootfn.org/1.8.1.rar", available: true),
    GameBuild(gameVersion: "1.8.2", link: "https://builds.rebootfn.org/1.8.2.rar", available: true),
    GameBuild(gameVersion: "1.9", link: "https://builds.rebootfn.org/1.9.rar", available: true),
    GameBuild(gameVersion: "1.9.1", link: "https://builds.rebootfn.org/1.9.1.rar", available: true),
    GameBuild(gameVersion: "1.10", link: "https://builds.rebootfn.org/1.10.rar", available: true),
    GameBuild(gameVersion: "1.11", link: "https://builds.rebootfn.org/1.11.zip", available: true),
    GameBuild(gameVersion: "2.1.0", link: "https://builds.rebootfn.org/2.1.0.zip", available: true),
    GameBuild(gameVersion: "2.2.0", link: "https://builds.rebootfn.org/2.2.0.rar", available: true),
    GameBuild(gameVersion: "2.3", link: "https://builds.rebootfn.org/2.3.rar", available: true),
    GameBuild(gameVersion: "2.4.0", link: "https://builds.rebootfn.org/2.4.0.zip", available: true),
    GameBuild(gameVersion: "2.4.2", link: "https://builds.rebootfn.org/2.4.2.zip", available: true),
    GameBuild(gameVersion: "2.5.0", link: "https://builds.rebootfn.org/2.5.0.rar", available: true),
    GameBuild(gameVersion: "3.0", link: "https://builds.rebootfn.org/3.0.zip", available: true),
    GameBuild(gameVersion: "3.1", link: "https://builds.rebootfn.org/3.1.rar", available: true),
    GameBuild(gameVersion: "3.1.1", link: "https://builds.rebootfn.org/3.1.1.zip", available: true),
    GameBuild(gameVersion: "3.2", link: "https://builds.rebootfn.org/3.2.zip", available: true),
    GameBuild(gameVersion: "3.3", link: "https://builds.rebootfn.org/3.3.rar", available: true),
    GameBuild(gameVersion: "3.5", link: "https://builds.rebootfn.org/3.5.rar", available: true),
    GameBuild(gameVersion: "3.6", link: "https://builds.rebootfn.org/3.6.zip", available: true),
    GameBuild(gameVersion: "4.0", link: "https://builds.rebootfn.org/4.0.zip", available: true),
    GameBuild(gameVersion: "4.1", link: "https://builds.rebootfn.org/4.1.zip", available: true),
    GameBuild(gameVersion: "4.2", link: "https://builds.rebootfn.org/4.2.zip", available: true),
    GameBuild(gameVersion: "4.4", link: "https://builds.rebootfn.org/4.4.rar", available: true),
    GameBuild(gameVersion: "4.5", link: "https://builds.rebootfn.org/4.5.rar", available: true),
    GameBuild(gameVersion: "5.00", link: "https://builds.rebootfn.org/5.00.rar", available: true),
    GameBuild(gameVersion: "5.0.1", link: "https://builds.rebootfn.org/5.0.1.rar", available: true),
    GameBuild(gameVersion: "5.10", link: "https://builds.rebootfn.org/5.10.rar", available: true),
    GameBuild(gameVersion: "5.21", link: "https://builds.rebootfn.org/5.21.rar", available: true),
    GameBuild(gameVersion: "5.30", link: "https://builds.rebootfn.org/5.30.rar", available: true),
    GameBuild(gameVersion: "5.40", link: "https://builds.rebootfn.org/5.40.rar", available: true),
    GameBuild(gameVersion: "6.00", link: "https://builds.rebootfn.org/6.00.rar", available: true),
    GameBuild(gameVersion: "6.01", link: "https://builds.rebootfn.org/6.01.rar", available: true),
    GameBuild(gameVersion: "6.1.1", link: "https://builds.rebootfn.org/6.1.1.rar", available: true),
    GameBuild(gameVersion: "6.02", link: "https://builds.rebootfn.org/6.02.rar", available: true),
    GameBuild(gameVersion: "6.2.1", link: "https://builds.rebootfn.org/6.2.1.rar", available: true),
    GameBuild(gameVersion: "6.10", link: "https://builds.rebootfn.org/6.10.rar", available: true),
    GameBuild(gameVersion: "6.10.1", link: "https://builds.rebootfn.org/6.10.1.rar", available: true),
    GameBuild(gameVersion: "6.10.2", link: "https://builds.rebootfn.org/6.10.2.rar", available: true),
    GameBuild(gameVersion: "6.21", link: "https://builds.rebootfn.org/6.21.rar", available: true),
    GameBuild(gameVersion: "6.22", link: "https://builds.rebootfn.org/6.22.rar", available: true),
    GameBuild(gameVersion: "6.30", link: "https://builds.rebootfn.org/6.30.rar", available: true),
    GameBuild(gameVersion: "6.31", link: "https://builds.rebootfn.org/6.31.rar", available: true),
    GameBuild(gameVersion: "7.00", link: "https://builds.rebootfn.org/7.00.rar", available: true),
    GameBuild(gameVersion: "7.10", link: "https://builds.rebootfn.org/7.10.rar", available: true),
    GameBuild(gameVersion: "7.20", link: "https://builds.rebootfn.org/7.20.rar", available: true),
    GameBuild(gameVersion: "7.30", link: "https://builds.rebootfn.org/7.30.zip", available: true),
    GameBuild(gameVersion: "7.40", link: "https://builds.rebootfn.org/7.40.rar", available: true),
    GameBuild(gameVersion: "8.00", link: "https://builds.rebootfn.org/8.00.zip", available: true),
    GameBuild(gameVersion: "8.20", link: "https://builds.rebootfn.org/8.20.rar", available: true),
    GameBuild(gameVersion: "8.30", link: "https://builds.rebootfn.org/8.30.rar", available: true),
    GameBuild(gameVersion: "8.40", link: "https://builds.rebootfn.org/8.40.zip", available: true),
    GameBuild(gameVersion: "8.50", link: "https://builds.rebootfn.org/8.50.zip", available: true),
    GameBuild(gameVersion: "8.51", link: "https://builds.rebootfn.org/8.51.rar", available: true),
    GameBuild(gameVersion: "9.00", link: "https://builds.rebootfn.org/9.00.zip", available: true),
    GameBuild(gameVersion: "9.01", link: "https://builds.rebootfn.org/9.01.zip", available: true),
    GameBuild(gameVersion: "9.10", link: "https://builds.rebootfn.org/9.10.rar", available: true),
    GameBuild(gameVersion: "9.21", link: "https://builds.rebootfn.org/9.21.zip", available: true),
    GameBuild(gameVersion: "9.30", link: "https://builds.rebootfn.org/9.30.zip", available: true),
    GameBuild(gameVersion: "9.40", link: "https://builds.rebootfn.org/9.40.zip", available: true),
    GameBuild(gameVersion: "9.41", link: "https://builds.rebootfn.org/9.41.rar", available: true),
    GameBuild(gameVersion: "10.00", link: "https://builds.rebootfn.org/10.00.zip", available: true),
    GameBuild(gameVersion: "10.10", link: "https://builds.rebootfn.org/10.10.zip", available: true),
    GameBuild(gameVersion: "10.20", link: "https://builds.rebootfn.org/10.20.zip", available: true),
    GameBuild(gameVersion: "10.31", link: "https://builds.rebootfn.org/10.31.zip", available: true),
    GameBuild(gameVersion: "10.40", link: "https://builds.rebootfn.org/10.40.rar", available: false),
    GameBuild(gameVersion: "11.00", link: "https://builds.rebootfn.org/11.00.zip", available: false),
    GameBuild(gameVersion: "11.31", link: "https://builds.rebootfn.org/11.31.rar", available: false),
    GameBuild(gameVersion: "12.00", link: "https://builds.rebootfn.org/12.00.rar", available: false),
    GameBuild(gameVersion: "12.21", link: "https://builds.rebootfn.org/12.21.zip", available: false),
    GameBuild(gameVersion: "Fortnite 12.41", link: "https://builds.rebootfn.org/Fortnite%2012.41.zip", available: false),
    GameBuild(gameVersion: "12.50", link: "https://builds.rebootfn.org/12.50.zip", available: false),
    GameBuild(gameVersion: "12.61", link: "https://builds.rebootfn.org/12.61.zip", available: false),
    GameBuild(gameVersion: "13.00", link: "https://builds.rebootfn.org/13.00.rar", available: false),
    GameBuild(gameVersion: "13.40", link: "https://builds.rebootfn.org/13.40.zip", available: false),
    GameBuild(gameVersion: "14.00", link: "https://builds.rebootfn.org/14.00.rar", available: false),
    GameBuild(gameVersion: "14.40", link: "https://builds.rebootfn.org/14.40.rar", available: false),
    GameBuild(gameVersion: "14.60", link: "https://builds.rebootfn.org/14.60.rar", available: false),
    GameBuild(gameVersion: "15.30", link: "https://builds.rebootfn.org/15.30.rar", available: false),
    GameBuild(gameVersion: "16.40", link: "https://builds.rebootfn.org/16.40.rar", available: false),
    GameBuild(gameVersion: "17.30", link: "https://builds.rebootfn.org/17.30.zip", available: false),
    GameBuild(gameVersion: "17.50", link: "https://builds.rebootfn.org/17.50.zip", available: false),
    GameBuild(gameVersion: "18.40", link: "https://builds.rebootfn.org/18.40.zip", available: false),
    GameBuild(gameVersion: "19.10", link: "https://builds.rebootfn.org/19.10.rar", available: false),
    GameBuild(gameVersion: "20.40", link: "https://builds.rebootfn.org/20.40.zip", available: false)
];


Future<void> downloadArchiveBuild(GameBuildDownloadOptions options) async {
    final fileName = options.build.link.substring(options.build.link.lastIndexOf("/") + 1);
    final outputFile = File("${options.destination.path}\\.build\\$fileName");
    Timer? timer;
    try {
        final stopped = _setupLifecycle(options);
        await outputFile.parent.create(recursive: true);

        final downloadItemCompleter = Completer<File>();

        await _startAriaServer();
        final downloadId = await _startAriaDownload(options, outputFile);
        timer = Timer.periodic(const Duration(seconds: 5), (Timer timer) async {
            try {
                final statusRequestId = Uuid().toString().replaceAll("-", "");
                final statusRequest = {
                    "jsonrcp": "2.0",
                    "id": statusRequestId,
                    "method": "aria2.tellStatus",
                    "params": [
                        downloadId
                    ]
                };
                final statusResponse = await http.post(_ariaEndpoint, body: jsonEncode(statusRequest));
                final statusResponseJson = jsonDecode(statusResponse.body) as Map?;
                if(statusResponseJson == null) {
                    downloadItemCompleter.completeError("Invalid download status (invalid JSON)");
                    timer.cancel();
                    return;
                }

                final result = statusResponseJson["result"];
                final files = result["files"] as List?;
                if(files == null || files.isEmpty) {
                    downloadItemCompleter.completeError("Download aborted");
                    timer.cancel();
                    return;
                }

                final error = result["errorCode"];
                if(error != null) {
                    final errorCode = int.tryParse(error);
                    if(errorCode == 0) {
                        final path = File(files[0]["path"]);
                        downloadItemCompleter.complete(path);
                    }else if(errorCode == 3) {
                        downloadItemCompleter.completeError("This build is not available yet");
                    }else {
                        final errorMessage = result["errorMessage"];
                        downloadItemCompleter.completeError("$errorMessage (error code $errorCode)");
                    }

                    timer.cancel();
                    return;
                }

                final speed = int.parse(result["downloadSpeed"] ?? "0");
                final completedLength = int.parse(files[0]["completedLength"] ?? "0");
                final totalLength = int.parse(files[0]["length"] ?? "0");

                final percentage = completedLength * 100 / totalLength;
                final minutesLeft = speed == 0 ? -1 : ((totalLength - completedLength) / speed / 60).round();
                _onProgress(
                    options.port,
                    percentage,
                    speed,
                    minutesLeft,
                    false
                );
            }catch(error) {
                throw "Invalid download status (${error})";
            }
        });

        await Future.any([stopped.future, downloadItemCompleter.future]);
        if(!stopped.isCompleted) {
            final extension = path.extension(fileName);
            await _extractArchive(stopped, extension, await downloadItemCompleter.future, options);
        }else {
            await _stopAriaDownload(downloadId);
        }
    }catch(error) {
        _onError(error, options);
    }finally {
        delete(outputFile);
        timer?.cancel();
    }
}

Future<void> _startAriaServer() async {
    await stopDownloadServer();
    final aria2c = File("${assetsDirectory.path}\\build\\aria2c.exe");
    if(!aria2c.existsSync()) {
        throw "Missing aria2c.exe at ${aria2c.path}";
    }

    final process = await startProcess(
        executable: aria2c,
        args: [
            "--max-connection-per-server=${Platform.numberOfProcessors}",
            "--split=${Platform.numberOfProcessors}",
            "--enable-rpc",
            "--rpc-listen-all=true",
            "--rpc-allow-origin-all",
            "--rpc-listen-port=$_ariaPort",
            "--file-allocation=none",
            "--check-certificate=false"
        ],
        window: false
    );
    process.stdOutput.listen((message) => log("[ARIA] Message: $message"));
    process.stdError.listen((error) => log("[ARIA] Error: $error"));
    process.exitCode.then((exitCode) => log("[ARIA] Exit code: $exitCode"));
    for(var i = 0; i < _ariaMaxSpawnTime.inSeconds; i++) {
        if(await _isAriaRunning()) {
            return;
        }
        await Future.delayed(const Duration(seconds: 1));
    }
    throw "cannot start download server (timeout exceeded)";
}

Future<bool> _isAriaRunning() async {
    try {
        final statusRequestId = Uuid().toString().replaceAll("-", "");
        final statusRequest = {
            "jsonrcp": "2.0",
            "id": statusRequestId,
            "method": "aria2.getVersion",
            "params": [

            ]
        };
        final response = await http.post(_ariaEndpoint, body: jsonEncode(statusRequest));
        return response.statusCode == 200;
    }catch(_) {
        return false;
    }
}

Future<String> _startAriaDownload(GameBuildDownloadOptions options, File outputFile) async {
    http.Response? addDownloadResponse;
    try {
        final addDownloadRequestId = Uuid().toString().replaceAll("-", "");
        final addDownloadRequest = {
            "jsonrcp": "2.0",
            "id": addDownloadRequestId,
            "method": "aria2.addUri",
            "params": [
                [options.build.link],
                {
                    "dir": outputFile.parent.path,
                    "out": path.basename(outputFile.path)
                }
            ]
        };
        addDownloadResponse = await http.post(_ariaEndpoint, body: jsonEncode(addDownloadRequest));
        final addDownloadResponseJson = jsonDecode(addDownloadResponse.body);
        final downloadId = addDownloadResponseJson is Map ? addDownloadResponseJson['result'] : null;
        if(downloadId == null) {
            throw "Start failed (${addDownloadResponse.body})";
        }

        return downloadId;
    }catch(error) {
        throw "Start failed (${addDownloadResponse?.body ?? error})";
    }
}

Future<void> _stopAriaDownload(String downloadId) async {
    try {
        final addDownloadRequestId = Uuid().toString().replaceAll("-", "");
        final addDownloadRequest = {
            "jsonrcp": "2.0",
            "id": addDownloadRequestId,
            "method": "aria2.forceRemove",
            "params": [
                downloadId
            ]
        };
        await http.post(_ariaEndpoint, body: jsonEncode(addDownloadRequest));
        stopDownloadServer();
    }catch(error) {
        throw "Stop failed (${error})";
    }
}

Future<void> stopDownloadServer() async {
    await killProcessByPort(_ariaPort);
}


Future<void> _extractArchive(Completer<dynamic> stopped, String extension, File tempFile, GameBuildDownloadOptions options) async {
    Process? process;
    switch (extension.toLowerCase()) {
        case ".zip":
            final sevenZip = File("${assetsDirectory.path}\\build\\7zip.exe");
            if(!sevenZip.existsSync()) {
                throw "Missing 7zip.exe";
            }

            process = await startProcess(
                executable: sevenZip,
                args: [
                    "x",
                    "-bsp1",
                    '-o"${options.destination.path}"',
                    "-y",
                    '"${tempFile.path}"'
                ],
            );
            var completed = false;
            process.stdOutput.listen((data) {
                if(data.toLowerCase().contains("everything is ok")) {
                    completed = true;
                    _onProgress(
                        options.port,
                        100,
                        0,
                        -1,
                        true
                    );
                    process?.kill(ProcessSignal.sigabrt);
                    return;
                }

                final element = data.trim().split(" ")[0];
                if(!element.endsWith("%")) {
                    return;
                }

                final percentage = int.parse(element.substring(0, element.length - 1)).toDouble();
                _onProgress(
                    options.port,
                    percentage,
                    0,
                    -1,
                    true
                );
            });
            process.stdError.listen((data) {
                if(!data.isBlankOrEmpty) {
                    _onError(data, options);
                }
            });
            process.exitCode.then((_) {
                if(!completed) {
                    _onError("Corrupted zip archive", options);
                }
            });
            break;
        case ".rar":
            final winrar = File("${assetsDirectory.path}\\build\\winrar.exe");
            if(!winrar.existsSync()) {
                throw "Missing winrar.exe";
            }

            process = await startProcess(
                executable: winrar,
                args: [
                    "x",
                    "-o+",
                    '"${tempFile.path}"',
                    "*.*",
                    '"${options.destination.path}"'
                ]
            );
            var completed = false;
            process.stdOutput.listen((data) {
                data = data.replaceAll("\r", "").replaceAll("\b", "").trim();
                if(data == "All OK") {
                    completed = true;
                    _onProgress(
                        options.port,
                        100,
                        0,
                        -1,
                        true
                    );
                    process?.kill(ProcessSignal.sigabrt);
                    return;
                }

                final element = _rarProgressRegex.firstMatch(data)?.group(1);
                if(element == null) {
                    return;
                }

                final percentage = int.parse(element).toDouble();
                _onProgress(
                    options.port,
                    percentage,
                    0,
                    -1,
                    true
                );
            });
            process.stdError.listen((data) {
                if(!data.isBlankOrEmpty) {
                    _onError(data, options);
                }
            });
            process.exitCode.then((_) {
                if(!completed) {
                    _onError("Corrupted rar archive", options);
                }
            });
            break;
        default:
            throw ArgumentError("Unexpected file extension: $extension}");
    }

    await Future.any([stopped.future, process.exitCode]);
    process.kill(ProcessSignal.sigabrt);
}

void _onProgress(SendPort port, double percentage, int speed, int minutesLeft, bool extracting) {
    if(percentage == 0) {
        port.send(GameBuildDownloadProgress(
            progress: percentage,
            extracting: extracting,
            timeLeft: null,
            speed: speed
        ));
        return;
    }

    port.send(GameBuildDownloadProgress(
        progress: percentage,
        extracting: extracting,
        timeLeft: minutesLeft,
        speed: speed
    ));
}

void _onError(Object? error, GameBuildDownloadOptions options) {
    if(error != null) {
        options.port.send(error.toString());
    }
}

Completer<dynamic> _setupLifecycle(GameBuildDownloadOptions options) {
    var stopped = Completer();
    var lifecyclePort = ReceivePort();
    lifecyclePort.listen((message) {
        if(message == kStopBuildDownloadSignal && !stopped.isCompleted) {
            lifecyclePort.close();
            stopped.complete();
        }
    });
    options.port.send(lifecyclePort.sendPort);
    return stopped;
}

Future<bool> hasRebootDllUpdate(int? lastUpdateMs, {int hours = 24, bool force = false}) async {
    final lastUpdate = await _getLastUpdate(lastUpdateMs);
    final exists = await rebootBeforeS20DllFile.exists() && await rebootAboveS20DllFile.exists();
    final now = DateTime.now();
    return force || !exists || (hours > 0 && lastUpdate != null && now.difference(lastUpdate).inHours > hours);
}

Future<bool> downloadDependency(GameDll dll, String outputPath) async {
    String? name;
    switch(dll) {
      case GameDll.console:
        name = "console.dll";
      case GameDll.auth:
          name = "cobalt.dll";
      case GameDll.memoryLeak:
        name = "memory.dll";
        case GameDll.gameServer:
         name = null;
    }
    if(name == null) {
        return false;
    }

    final response = await http.get(Uri.parse("https://github.com/Auties00/reboot_launcher/raw/master/gui/dependencies/dlls/$name"));
    if(response.statusCode != 200) {
        throw Exception("Cannot download $name: status code ${response.statusCode}");
    }

    final output = File(outputPath);
    await output.parent.create(recursive: true);
    await output.writeAsBytes(response.bodyBytes, flush: true);
    try {
        await output.readAsBytes();
        return true;
    }catch(_) {
        return false;
    }
}

Future<bool> downloadRebootDll(File file, String url, bool aboveS20) async {
    Directory? outputDir;
    try {
        var response = await http.get(Uri.parse(url));
        if(response.statusCode != 200) {
            response = await http.get(Uri.parse(aboveS20 ? _kRebootAboveS20FallbackDownloadUrl : _kRebootBelowS20FallbackDownloadUrl));
            if(response.statusCode != 200) {
                throw "status code ${response.statusCode}";
            }
        }

        outputDir = await installationDirectory.createTemp("reboot_out");
        final tempZip = File("${outputDir.path}\\reboot.zip");

        try {
            await tempZip.writeAsBytes(response.bodyBytes, flush: true); // Write reboot.zip to disk

            await tempZip.readAsBytes(); // Check implicitly if antivirus doesn't like reboot

            await extractFileToDisk(tempZip.path, outputDir.path);

            final rebootDll = outputDir.listSync()
                .firstWhere((element) => path.extension(element.path) == ".dll") as File;
            final rebootDllSource = await rebootDll.readAsBytes();
            await file.writeAsBytes(rebootDllSource, flush: true);

            await file.readAsBytes(); // Check implicitly if antivirus doesn't like reboot

            return true;
        } catch(_) {
            return false; // Anti virus probably flagged reboot
        }
    } finally{
        if(outputDir != null) {
            delete(outputDir);
        }
    }
}

Future<DateTime?> _getLastUpdate(int? lastUpdateMs) async {
  return lastUpdateMs != null
      ? DateTime.fromMillisecondsSinceEpoch(lastUpdateMs)
      : null;
}