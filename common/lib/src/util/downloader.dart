import 'dart:io';

import 'package:archive/archive_io.dart';
import 'package:http/http.dart' as http;
import 'package:path/path.dart' as path;
import 'package:reboot_common/common.dart';
import 'dart:async';
import 'dart:convert';
import 'dart:isolate';

import 'package:reboot_common/src/extension/types.dart';
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
final List<FortniteBuild> downloadableBuilds = [
    FortniteBuild(gameVersion: "1.7.2", link: "https://public.simplyblk.xyz/1.7.2.zip", available: true),
    FortniteBuild(gameVersion: "1.8", link: "https://public.simplyblk.xyz/1.8.rar", available: true),
    FortniteBuild(gameVersion: "1.8.1", link: "https://public.simplyblk.xyz/1.8.1.rar", available: true),
    FortniteBuild(gameVersion: "1.8.2", link: "https://public.simplyblk.xyz/1.8.2.rar", available: true),
    FortniteBuild(gameVersion: "1.9", link: "https://public.simplyblk.xyz/1.9.rar", available: true),
    FortniteBuild(gameVersion: "1.9.1", link: "https://public.simplyblk.xyz/1.9.1.rar", available: true),
    FortniteBuild(gameVersion: "1.10", link: "https://public.simplyblk.xyz/1.10.rar", available: true),
    FortniteBuild(gameVersion: "1.11", link: "https://public.simplyblk.xyz/1.11.zip", available: true),
    FortniteBuild(gameVersion: "2.1.0", link: "https://public.simplyblk.xyz/2.1.0.zip", available: true),
    FortniteBuild(gameVersion: "2.2.0", link: "https://public.simplyblk.xyz/2.2.0.rar", available: true),
    FortniteBuild(gameVersion: "2.3", link: "https://public.simplyblk.xyz/2.3.rar", available: true),
    FortniteBuild(gameVersion: "2.4.0", link: "https://public.simplyblk.xyz/2.4.0.zip", available: true),
    FortniteBuild(gameVersion: "2.4.2", link: "https://public.simplyblk.xyz/2.4.2.zip", available: true),
    FortniteBuild(gameVersion: "2.5.0", link: "https://public.simplyblk.xyz/2.5.0.rar", available: true),
    FortniteBuild(gameVersion: "3.0", link: "https://public.simplyblk.xyz/3.0.zip", available: true),
    FortniteBuild(gameVersion: "3.1", link: "https://public.simplyblk.xyz/3.1.rar", available: true),
    FortniteBuild(gameVersion: "3.1.1", link: "https://public.simplyblk.xyz/3.1.1.zip", available: true),
    FortniteBuild(gameVersion: "3.2", link: "https://public.simplyblk.xyz/3.2.zip", available: true),
    FortniteBuild(gameVersion: "3.3", link: "https://public.simplyblk.xyz/3.3.rar", available: true),
    FortniteBuild(gameVersion: "3.5", link: "https://public.simplyblk.xyz/3.5.rar", available: true),
    FortniteBuild(gameVersion: "3.6", link: "https://public.simplyblk.xyz/3.6.zip", available: true),
    FortniteBuild(gameVersion: "4.0", link: "https://public.simplyblk.xyz/4.0.zip", available: true),
    FortniteBuild(gameVersion: "4.1", link: "https://public.simplyblk.xyz/4.1.zip", available: true),
    FortniteBuild(gameVersion: "4.2", link: "https://public.simplyblk.xyz/4.2.zip", available: true),
    FortniteBuild(gameVersion: "4.4", link: "https://public.simplyblk.xyz/4.4.rar", available: true),
    FortniteBuild(gameVersion: "4.5", link: "https://public.simplyblk.xyz/4.5.rar", available: true),
    FortniteBuild(gameVersion: "5.00", link: "https://public.simplyblk.xyz/5.00.rar", available: true),
    FortniteBuild(gameVersion: "5.0.1", link: "https://public.simplyblk.xyz/5.0.1.rar", available: true),
    FortniteBuild(gameVersion: "5.10", link: "https://public.simplyblk.xyz/5.10.rar", available: true),
    FortniteBuild(gameVersion: "5.21", link: "https://public.simplyblk.xyz/5.21.rar", available: true),
    FortniteBuild(gameVersion: "5.30", link: "https://public.simplyblk.xyz/5.30.rar", available: true),
    FortniteBuild(gameVersion: "5.40", link: "https://public.simplyblk.xyz/5.40.rar", available: true),
    FortniteBuild(gameVersion: "6.00", link: "https://public.simplyblk.xyz/6.00.rar", available: true),
    FortniteBuild(gameVersion: "6.01", link: "https://public.simplyblk.xyz/6.01.rar", available: true),
    FortniteBuild(gameVersion: "6.1.1", link: "https://public.simplyblk.xyz/6.1.1.rar", available: true),
    FortniteBuild(gameVersion: "6.02", link: "https://public.simplyblk.xyz/6.02.rar", available: true),
    FortniteBuild(gameVersion: "6.2.1", link: "https://public.simplyblk.xyz/6.2.1.rar", available: true),
    FortniteBuild(gameVersion: "6.10", link: "https://public.simplyblk.xyz/6.10.rar", available: true),
    FortniteBuild(gameVersion: "6.10.1", link: "https://public.simplyblk.xyz/6.10.1.rar", available: true),
    FortniteBuild(gameVersion: "6.10.2", link: "https://public.simplyblk.xyz/6.10.2.rar", available: true),
    FortniteBuild(gameVersion: "6.21", link: "https://public.simplyblk.xyz/6.21.rar", available: true),
    FortniteBuild(gameVersion: "6.22", link: "https://public.simplyblk.xyz/6.22.rar", available: true),
    FortniteBuild(gameVersion: "6.30", link: "https://public.simplyblk.xyz/6.30.rar", available: true),
    FortniteBuild(gameVersion: "6.31", link: "https://public.simplyblk.xyz/6.31.rar", available: true),
    FortniteBuild(gameVersion: "7.00", link: "https://public.simplyblk.xyz/7.00.rar", available: true),
    FortniteBuild(gameVersion: "7.10", link: "https://public.simplyblk.xyz/7.10.rar", available: true),
    FortniteBuild(gameVersion: "7.20", link: "https://public.simplyblk.xyz/7.20.rar", available: true),
    FortniteBuild(gameVersion: "7.30", link: "https://public.simplyblk.xyz/7.30.zip", available: true),
    FortniteBuild(gameVersion: "7.40", link: "https://public.simplyblk.xyz/7.40.rar", available: true),
    FortniteBuild(gameVersion: "8.00", link: "https://public.simplyblk.xyz/8.00.zip", available: true),
    FortniteBuild(gameVersion: "8.20", link: "https://public.simplyblk.xyz/8.20.rar", available: true),
    FortniteBuild(gameVersion: "8.30", link: "https://public.simplyblk.xyz/8.30.rar", available: true),
    FortniteBuild(gameVersion: "8.40", link: "https://public.simplyblk.xyz/8.40.zip", available: true),
    FortniteBuild(gameVersion: "8.50", link: "https://public.simplyblk.xyz/8.50.zip", available: true),
    FortniteBuild(gameVersion: "8.51", link: "https://public.simplyblk.xyz/8.51.rar", available: true),
    FortniteBuild(gameVersion: "9.00", link: "https://public.simplyblk.xyz/9.00.zip", available: true),
    FortniteBuild(gameVersion: "9.01", link: "https://public.simplyblk.xyz/9.01.zip", available: true),
    FortniteBuild(gameVersion: "9.10", link: "https://public.simplyblk.xyz/9.10.rar", available: true),
    FortniteBuild(gameVersion: "9.21", link: "https://public.simplyblk.xyz/9.21.zip", available: true),
    FortniteBuild(gameVersion: "9.30", link: "https://public.simplyblk.xyz/9.30.zip", available: true),
    FortniteBuild(gameVersion: "9.40", link: "https://public.simplyblk.xyz/9.40.zip", available: true),
    FortniteBuild(gameVersion: "9.41", link: "https://public.simplyblk.xyz/9.41.rar", available: true),
    FortniteBuild(gameVersion: "10.00", link: "https://public.simplyblk.xyz/10.00.zip", available: true),
    FortniteBuild(gameVersion: "10.10", link: "https://public.simplyblk.xyz/10.10.zip", available: true),
    FortniteBuild(gameVersion: "10.20", link: "https://public.simplyblk.xyz/10.20.zip", available: true),
    FortniteBuild(gameVersion: "10.31", link: "https://public.simplyblk.xyz/10.31.zip", available: true),
    FortniteBuild(gameVersion: "10.40", link: "https://public.simplyblk.xyz/10.40.rar", available: true),
    FortniteBuild(gameVersion: "11.00", link: "https://public.simplyblk.xyz/11.00.zip", available: true),
    FortniteBuild(gameVersion: "11.31", link: "https://public.simplyblk.xyz/11.31.rar", available: true),
    FortniteBuild(gameVersion: "12.00", link: "https://public.simplyblk.xyz/12.00.rar", available: true),
    FortniteBuild(gameVersion: "12.21", link: "https://public.simplyblk.xyz/12.21.zip", available: true),
    FortniteBuild(gameVersion: "12.50", link: "https://public.simplyblk.xyz/12.50.zip", available: true),
    FortniteBuild(gameVersion: "12.61", link: "https://public.simplyblk.xyz/12.61.zip", available: true),
    FortniteBuild(gameVersion: "13.00", link: "https://public.simplyblk.xyz/13.00.rar", available: true),
    FortniteBuild(gameVersion: "13.40", link: "https://public.simplyblk.xyz/13.40.zip", available: true),
    FortniteBuild(gameVersion: "14.00", link: "https://public.simplyblk.xyz/14.00.rar", available: true),
    FortniteBuild(gameVersion: "14.40", link: "https://public.simplyblk.xyz/14.40.rar", available: true),
    FortniteBuild(gameVersion: "14.60", link: "https://public.simplyblk.xyz/14.60.rar", available: true),
    FortniteBuild(gameVersion: "15.30", link: "https://public.simplyblk.xyz/15.30.rar", available: true),
    FortniteBuild(gameVersion: "16.40", link: "https://public.simplyblk.xyz/16.40.rar", available: true),
    FortniteBuild(gameVersion: "17.30", link: "https://public.simplyblk.xyz/17.30.zip", available: true),
    FortniteBuild(gameVersion: "17.50", link: "https://public.simplyblk.xyz/17.50.zip", available: true),
    FortniteBuild(gameVersion: "18.40", link: "https://public.simplyblk.xyz/18.40.zip", available: true),
    FortniteBuild(gameVersion: "19.10", link: "https://public.simplyblk.xyz/19.10.rar", available: true),
    FortniteBuild(gameVersion: "20.40", link: "https://public.simplyblk.xyz/20.40.zip", available: true),
];


Future<void> downloadArchiveBuild(FortniteBuildDownloadOptions options) async {
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

Future<String> _startAriaDownload(FortniteBuildDownloadOptions options, File outputFile) async {
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


Future<void> _extractArchive(Completer<dynamic> stopped, String extension, File tempFile, FortniteBuildDownloadOptions options) async {
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
                if(!data.isBlank) {
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
                if(!data.isBlank) {
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
        port.send(FortniteBuildDownloadProgress(
            progress: percentage,
            extracting: extracting,
            timeLeft: null,
            speed: speed
        ));
        return;
    }

    port.send(FortniteBuildDownloadProgress(
        progress: percentage,
        extracting: extracting,
        timeLeft: minutesLeft,
        speed: speed
    ));
}

void _onError(Object? error, FortniteBuildDownloadOptions options) {
    if(error != null) {
        options.port.send(error.toString());
    }
}

Completer<dynamic> _setupLifecycle(FortniteBuildDownloadOptions options) {
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

Future<bool> downloadDependency(InjectableDll dll, String outputPath) async {
    String? name;
    switch(dll) {
      case InjectableDll.console:
        name = "console.dll";
      case InjectableDll.auth:
          name = "cobalt.dll";
      case InjectableDll.memoryLeak:
        name = "memory.dll";
        case InjectableDll.gameServer:
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

Future<void> downloadRebootDll(File file, String url, bool aboveS20) async {
    Directory? outputDir;
    try {
        var response = await http.get(Uri.parse(url));
        if(response.statusCode != 200) {
            response = await http.get(Uri.parse(aboveS20 ? _kRebootAboveS20FallbackDownloadUrl : _kRebootBelowS20FallbackDownloadUrl));
            if(response.statusCode != 200) {
                throw Exception("status code ${response.statusCode}");
            }
        }

        outputDir = await installationDirectory.createTemp("reboot_out");
        final tempZip = File("${outputDir.path}\\reboot.zip");
        await tempZip.writeAsBytes(response.bodyBytes, flush: true);
        await extractFileToDisk(tempZip.path, outputDir.path);
        final rebootDll = File(outputDir.listSync().firstWhere((element) => path.extension(element.path) == ".dll").path);
        await file.writeAsBytes(await rebootDll.readAsBytes(), flush: true);
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