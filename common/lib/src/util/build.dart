import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:isolate';

import 'package:dio/dio.dart';
import 'package:dio/io.dart';
import 'package:path/path.dart' as path;
import 'package:reboot_common/common.dart';
import 'package:reboot_common/src/extension/types.dart';
import 'package:version/version.dart';

const String kStopBuildDownloadSignal = "kill";

final Dio _dio = _buildDioInstance();
Dio _buildDioInstance() {
  final dio = Dio();
  final httpClientAdapter = dio.httpClientAdapter as IOHttpClientAdapter;
  httpClientAdapter.createHttpClient = () {
    final client = HttpClient();
    client.badCertificateCallback = (X509Certificate cert, String host, int port) => true;
    return client;
  };
  return dio;
}

final String _archiveSourceUrl = "https://raw.githubusercontent.com/simplyblk/Fortnitebuilds/main/README.md";
final RegExp _rarProgressRegex = RegExp("^((100)|(\\d{1,2}(.\\d*)?))%\$");
const String _deniedConnectionError = "The connection was denied: your firewall might be blocking the download";
const String _unavailableError = "The build downloader is not available right now";
const String _genericError = "The build downloader is not working correctly";
const int _maxErrors = 100;

Future<List<FortniteBuild>> fetchBuilds(ignored) async {
  final response = await _dio.get<String>(
      _archiveSourceUrl,
      options: Options(
          responseType: ResponseType.plain
      )
  );
  if (response.statusCode != 200) {
    return [];
  }

  var results = <FortniteBuild>[];
  for (final line in response.data?.split("\n") ?? []) {
    if (!line.startsWith("|")) {
      continue;
    }

    var parts = line.substring(1, line.length - 1).split("|");
    if (parts.isEmpty) {
      continue;
    }

    var versionName = parts.first.trim();
    final separator = versionName.indexOf("-");
    if(separator != -1) {
      versionName = versionName.substring(0, separator);
    }

    final link = parts.last.trim();
    try {
      results.add(FortniteBuild(
          version: Version.parse(versionName),
          link: link,
          available: link.endsWith(".zip") || link.endsWith(".rar")
      ));
    } on FormatException {
      // Ignore
    }
  }

  return results;
}


Future<void> downloadArchiveBuild(FortniteBuildDownloadOptions options) async {
  try {
    final stopped = _setupLifecycle(options);
    final outputDir = Directory("${options.destination.path}\\.build");
    await outputDir.create(recursive: true);
    final fileName = options.build.link.substring(options.build.link.lastIndexOf("/") + 1);
    final extension = path.extension(fileName);
    final tempFile = File("${outputDir.path}\\$fileName");
    if(await tempFile.exists()) {
      await tempFile.delete(recursive: true);
    }

    final startTime = DateTime.now().millisecondsSinceEpoch;
    final response = _downloadArchive(options, stopped, tempFile, startTime);
    await Future.any([stopped.future, response]);
    if(!stopped.isCompleted) {
      await _extractArchive(stopped, extension, tempFile, options);
    }

    delete(outputDir);
  }catch(error) {
    _onError(error, options);
  }
}

Future<void> _downloadArchive(FortniteBuildDownloadOptions options, Completer stopped, File tempFile, int startTime, [int? byteStart = null, int errorsCount = 0]) async {
  var received = byteStart ?? 0;
  try {
    await _dio.download(
        options.build.link,
        tempFile.path,
        onReceiveProgress: (data, length) {
          if(stopped.isCompleted) {
            throw StateError("Download interrupted");
          }

          received = data;
          final percentage = (received / length) * 100;
          _onProgress(startTime, percentage < 1 ? null : DateTime.now().millisecondsSinceEpoch, percentage, false, options);
        },
        deleteOnError: false,
        options: Options(
          validateStatus: (statusCode) {
            if(statusCode == 200) {
              return true;
            }

            if(statusCode == 403 || statusCode == 503) {
              throw _deniedConnectionError;
            }

            if(statusCode == 404) {
              throw _unavailableError;
            }

            throw _genericError;
          },
          headers: byteStart == null || byteStart <= 0 ? {
            "Cookie": "_c_t_c=1"
          } :  {
            "Cookie": "_c_t_c=1",
            "Range": "bytes=${byteStart}-"
          },
        )
    );
  }catch(error) {
    if(stopped.isCompleted) {
      return;
    }

    if(errorsCount > _maxErrors || error.toString().contains(_deniedConnectionError) || error.toString().contains(_unavailableError)) {
      _onError(error, options);
      return;
    }

    await _downloadArchive(options, stopped, tempFile, startTime, received, errorsCount + 1);
  }
}

Future<void> _extractArchive(Completer<dynamic> stopped, String extension, File tempFile, FortniteBuildDownloadOptions options) async {
  final startTime = DateTime.now().millisecondsSinceEpoch;
  Process? process;
  switch (extension.toLowerCase()) {
    case ".zip":
      final sevenZip = File("${assetsDirectory.path}\\build\\7zip.exe");
      if(!sevenZip.existsSync()) {
        throw "Corrupted installation: missing 7zip.exe";
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
        final now = DateTime.now().millisecondsSinceEpoch;
        if(data.toLowerCase().contains("everything is ok")) {
          completed = true;
          _onProgress(startTime, now, 100, true, options);
          process?.kill(ProcessSignal.sigabrt);
          return;
        }

        final element = data.trim().split(" ")[0];
        if(!element.endsWith("%")) {
          return;
        }

        final percentage = int.parse(element.substring(0, element.length - 1)).toDouble();
        _onProgress(startTime, now, percentage, true, options);
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
        throw "Corrupted installation: missing winrar.exe";
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
        final now = DateTime.now().millisecondsSinceEpoch;
        data = data.replaceAll("\r", "").replaceAll("\b", "").trim();
        if(data == "All OK") {
          completed = true;
          _onProgress(startTime, now, 100, true, options);
          process?.kill(ProcessSignal.sigabrt);
          return;
        }

        final element = _rarProgressRegex.firstMatch(data)?.group(1);
        if(element == null) {
          return;
        }

        final percentage = int.parse(element).toDouble();
        _onProgress(startTime, now, percentage, true, options);
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

void _onProgress(int startTime, int? now, double percentage, bool extracting, FortniteBuildDownloadOptions options) {
  if(percentage == 0) {
    options.port.send(FortniteBuildDownloadProgress(
        progress: percentage,
        extracting: extracting
    ));
    return;
  }

  final msLeft = now == null ? null : startTime + (now - startTime) * 100 / percentage - now;
  final minutesLeft = msLeft == null ? null : (msLeft / 1000 / 60).round();
  options.port.send(FortniteBuildDownloadProgress(
      progress: percentage,
      extracting: extracting,
      minutesLeft: minutesLeft
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