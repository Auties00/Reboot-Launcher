import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:isolate';

import 'package:path/path.dart' as path;
import 'package:reboot_common/common.dart';
import 'package:dio/dio.dart';

final Dio _dio = Dio();
final String _manifestSourceUrl = "https://raw.githubusercontent.com/simplyblk/Fortnitebuilds/main/README.md";
final RegExp _rarProgressRegex = RegExp("^((100)|(\\d{1,2}(.\\d*)?))%\$");

Future<List<FortniteBuild>> fetchBuilds(ignored) async {
  var response = await _dio.get<String>(
      _manifestSourceUrl,
      options: Options(
          responseType: ResponseType.plain
      )
  );
  if (response.statusCode != 200) {
    throw Exception("Erroneous status code: ${response.statusCode}");
  }

  var results = <FortniteBuild>[];
  for (var line in response.data?.split("\n") ?? []) {
    if(!line.startsWith("|")) {
      continue;
    }

    var parts = line.substring(1, line.length - 1).split("|");
    if(parts.isEmpty) {
      continue;
    }

    var link = parts.last.trim();
    if(!link.endsWith(".zip") && !link.endsWith(".rar")) {
      continue;
    }

    var version = parts.first.trim();
    version = version.substring(0, version.indexOf("-"));
    results.add(FortniteBuild(version: "Fortnite $version", link: link));
  }

  return results;
}


Future<void> downloadArchiveBuild(ArchiveDownloadOptions options) async {
  var stopped = _setupLifecycle(options);
  var outputDir = Directory("${options.destination.path}\\.build");
  outputDir.createSync(recursive: true);
  options.destination.createSync(recursive: true);
  var fileName = options.archiveUrl.substring(options.archiveUrl.lastIndexOf("/") + 1);
  var extension = path.extension(fileName);
  var tempFile = File("${outputDir.path}\\$fileName");
  if(tempFile.existsSync()) {
    tempFile.deleteSync(recursive: true);
  }

  var startTime = DateTime.now().millisecondsSinceEpoch;
  var response = _downloadFile(options, tempFile, startTime);
  await Future.any([stopped.future, response]);
  if(!stopped.isCompleted) {
    var awaitedResponse = await response;
    if (!awaitedResponse.statusCode.toString().startsWith("20")) {
      throw Exception("Erroneous status code: ${awaitedResponse.statusCode}");
    }

    await _extract(stopped, extension, tempFile, options);
  }

  delete(outputDir);
}

Future<Response> _downloadFile(ArchiveDownloadOptions options, File tempFile, int startTime, [int? byteStart = null]) {
  var received = byteStart ?? 0;
  return _dio.download(
      options.archiveUrl,
      tempFile.path,
      onReceiveProgress: (data, length) {
        received = data;
        var now = DateTime.now();
        var progress = (received / length) * 100;
        var msLeft = startTime + (now.millisecondsSinceEpoch - startTime) * length / received - now.millisecondsSinceEpoch;
        var minutesLeft = (msLeft / 1000 / 60).round();
        options.port.send(ArchiveDownloadProgress(progress, minutesLeft, false));
      },
      deleteOnError: false,
      options: Options(
          headers: byteStart == null ? null : {
            "Range": "bytes=${byteStart}-"
          }
      )
  ).catchError((error) => _downloadFile(options, tempFile, startTime, received));
}

Future<void> _extract(Completer<dynamic> stopped, String extension, File tempFile, ArchiveDownloadOptions options) async {
  var startTime = DateTime.now().millisecondsSinceEpoch;
  Process? process;
  switch (extension.toLowerCase()) {
    case ".zip":
      process = await Process.start(
          "${assetsDirectory.path}\\build\\7zip.exe",
          ["a", "-bsp1", '-o"${options.destination.path}"', tempFile.path]
      );
      process.stdout.listen((bytes) {
        var now = DateTime.now().millisecondsSinceEpoch;
        var data = utf8.decode(bytes);
        if(data == "Everything is Ok") {
          options.port.send(ArchiveDownloadProgress(100, 0, true));
          return;
        }

        var element = data.trim().split(" ")[0];
        if(!element.endsWith("%")) {
          return;
        }

        var percentage = int.parse(element.substring(0, element.length - 1));
        if(percentage == 0) {
          options.port.send(ArchiveDownloadProgress(percentage.toDouble(), null, true));
          return;
        }

        _onProgress(startTime, now, percentage, options);
      });
      break;
    case ".rar":
      process = await Process.start(
          "${assetsDirectory.path}\\build\\winrar.exe",
          ["x", "-o+", tempFile.path, "*.*", options.destination.path]
      );
      process.stdout.listen((event) {
        var now = DateTime.now().millisecondsSinceEpoch;
        var data = utf8.decode(event);
        data.replaceAll("\r", "")
            .replaceAll("\b", "")
            .trim()
            .split("\n")
            .forEach((entry) {
                if(entry == "All OK") {
                  options.port.send(ArchiveDownloadProgress(100, 0, true));
                  return;
                }

                var element = _rarProgressRegex.firstMatch(entry)?.group(1);
                if(element == null) {
                  return;
                }

                var percentage = int.parse(element);
                if(percentage == 0) {
                  options.port.send(ArchiveDownloadProgress(percentage.toDouble(), null, true));
                  return;
                }

                _onProgress(startTime, now, percentage, options);
            });
      });
      process.stderr.listen((event) {
        var data = utf8.decode(event);
        options.port.send(data);
      });
      break;
    default:
      throw ArgumentError("Unexpected file extension: $extension}");
  }

  await Future.any([stopped.future, process.exitCode]);
}

void _onProgress(int startTime, int now, int percentage, ArchiveDownloadOptions options) {
    var msLeft = startTime + (now - startTime) * 100 / percentage - now;
  var minutesLeft = (msLeft / 1000 / 60).round();
  options.port.send(ArchiveDownloadProgress(percentage.toDouble(), minutesLeft, true));
}

Completer<dynamic> _setupLifecycle(ArchiveDownloadOptions options) {
  var stopped = Completer();
  var lifecyclePort = ReceivePort();
  lifecyclePort.listen((message) {
    if(message == "kill") {
      stopped.complete();
    }
  });
  options.port.send(lifecyclePort.sendPort);
  return stopped;
}