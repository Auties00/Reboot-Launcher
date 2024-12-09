import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:isolate';

import 'package:path/path.dart' as path;
import 'package:reboot_common/common.dart';
import 'package:reboot_common/src/extension/types.dart';
import 'package:uuid/uuid.dart';
import 'package:version/version.dart';

import 'package:http/http.dart' as http;

const String kStopBuildDownloadSignal = "kill";

final Uri _archiveSourceUrl = Uri.parse("https://builds.rebootfn.org/versions.json");
final int _ariaPort = 6800;
final Uri _ariaEndpoint = Uri.parse('http://localhost:$_ariaPort/jsonrpc');
final Duration _ariaMaxSpawnTime = const Duration(seconds: 10);
final String _ariaSecret = "RebootLauncher";
final RegExp _rarProgressRegex = RegExp("^((100)|(\\d{1,2}(.\\d*)?))%\$");

Future<List<FortniteBuild>> fetchBuilds(ignored) async {
  final response = await http.get(_archiveSourceUrl);
  if (response.statusCode != 200) {
    return [];
  }

  return jsonDecode(response.body)
      .map((entry) {
        try {
          final fileUrl = entry as String;
          final fileName = Uri.parse(fileUrl).pathSegments.last;
          final fileNameWithoutExtension = path.basenameWithoutExtension(fileName);
          return FortniteBuild(
              version: Version.parse(fileNameWithoutExtension),
              link: entry,
              available: true
          );
        }catch(_) {
          return null;
        }
      })
      .whereType<FortniteBuild>()
      .toList();
}

Future<void> downloadArchiveBuild(FortniteBuildDownloadOptions options) async {
  final fileName = options.build.link.substring(options.build.link.lastIndexOf("/") + 1);
  final outputFile = File("${options.destination.path}\\.build\\$fileName");
  try {
    final stopped = _setupLifecycle(options);
    await outputFile.parent.create(recursive: true);

    final downloadItemCompleter = Completer<File>();

    await _startAriaServer();
    final downloadId = await _startAriaDownload(options, outputFile);
    Timer.periodic(const Duration(seconds: 5), (Timer timer) async {
      try {
        final statusRequestId = Uuid().toString().replaceAll("-", "");
        final statusRequest = {
          "jsonrcp": "2.0",
          "id": statusRequestId,
          "method": "aria2.tellStatus",
          "params": [
            "token:${_ariaSecret}",
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
  }
}

Future<void> _startAriaServer() async {
  final running = await _isAriaRunning();
  if(running) {
    await killProcessByPort(_ariaPort);
  }

  final aria2c = File("${assetsDirectory.path}\\build\\aria2c.exe");
  if(!aria2c.existsSync()) {
    throw "Missing aria2c.exe";
  }

  await startProcess(
      executable: aria2c,
      args: [
        "--max-connection-per-server=${Platform.numberOfProcessors}",
        "--split=${Platform.numberOfProcessors}",
        "--enable-rpc",
        "--rpc-listen-all=true",
        "--rpc-allow-origin-all",
        "--rpc-secret=$_ariaSecret",
        "--rpc-listen-port=$_ariaPort"
      ],
    window: false
  );
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
        "token:${_ariaSecret}"
      ]
    };
    await http.post(_ariaEndpoint, body: jsonEncode(statusRequest));
    return true;
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
        "token:${_ariaSecret}",
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
        "token:${_ariaSecret}",
        downloadId
      ]
    };
    await http.post(_ariaEndpoint, body: jsonEncode(addDownloadRequest));
  }catch(error) {
    throw "Stop failed (${error})";
  }
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
      stopped.complete();
    }
  });
  options.port.send(lifecyclePort.sendPort);
  return stopped;
}

