import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:isolate';
import 'dart:typed_data';

import 'package:dio/dio.dart';
import 'package:path/path.dart' as path;
import 'package:reboot_common/common.dart';

const String kStopBuildDownloadSignal = "kill";

final Dio _dio = Dio();
final String _archiveSourceUrl = "https://raw.githubusercontent.com/simplyblk/Fortnitebuilds/main/README.md";
final RegExp _rarProgressRegex = RegExp("^((100)|(\\d{1,2}(.\\d*)?))%\$");
const String _manifestSourceUrl = "https://manifest.fnbuilds.services";
const int _maxDownloadErrors = 30;

Future<List<FortniteBuild>> fetchBuilds(ignored) async {
  final results = await Future.wait([_fetchManifestBuilds(), _fetchArchiveBuilds()]);
  final data =  <FortniteBuild>[];
  for(final result in results) {
    data.addAll(result);
  }

  return data;
}

Future<List<FortniteBuild>> _fetchManifestBuilds() async {
  try {
    final response = await _dio.get<String>("$_manifestSourceUrl/versions.json");
    final body = response.data;
    return jsonDecode(body!).map((version) {
      final nameParts = version.split("-");
      if(nameParts.length < 2) {
        return null;
      }

      final name = nameParts[1];
      return FortniteBuild(
          identifier: name,
          version: "Fortnite ${name}",
          link: "$_manifestSourceUrl/$name/$name.manifest",
          source: FortniteBuildSource.manifest
      );
    }).whereType<FortniteBuild>().toList();
  }catch(_) {
    return [];
  }
}

Future<List<FortniteBuild>> _fetchArchiveBuilds() async {
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
    results.add(FortniteBuild(
        identifier: version,
        version: "Fortnite $version",
        link: link,
        source: FortniteBuildSource.archive
    ));
  }

  return results;
}


Future<void> downloadArchiveBuild(FortniteBuildDownloadOptions options) async {
  try {
    final stopped = _setupLifecycle(options);
    switch(options.build.source) {
      case FortniteBuildSource.archive:
        final outputDir = Directory("${options.destination.path}\\.build");
        await outputDir.create(recursive: true);
        final fileName = options.build.link.substring(options.build.link.lastIndexOf("/") + 1);
        final extension = path.extension(fileName);
        final tempFile = File("${outputDir.path}\\$fileName");
        if(await tempFile.exists()) {
          await tempFile.delete(recursive: true);
        }

        final startTime = DateTime.now().millisecondsSinceEpoch;
        final response = _downloadArchive(options, tempFile, startTime);
        await Future.any([stopped.future, response]);
        if(!stopped.isCompleted) {
          var awaitedResponse = await response;
          if (!awaitedResponse.statusCode.toString().startsWith("20")) {
            options.port.send("Erroneous status code: ${awaitedResponse.statusCode}");
            return;
          }

          await _extractArchive(stopped, extension, tempFile, options);
        }

        delete(outputDir);
        break;
      case FortniteBuildSource.manifest:
        final response = await _dio.get<String>(options.build.link);
        final manifest = FortniteBuildManifestFile.fromJson(jsonDecode(response.data!));

        final totalBytes = manifest.size;
        final outputDir = options.destination;
        await outputDir.create(recursive: true);

        final startTime = DateTime.now().millisecondsSinceEpoch;
        final codec = GZipCodec();
        var completedBytes = 0;
        var lastPercentage = 0.0;

        final writers = manifest.chunks.map((chunkedFile) async {
          final outputFile = File('${outputDir.path}/${chunkedFile.file}');
          if(outputFile.existsSync()) {
            if(outputFile.lengthSync() != chunkedFile.fileSize) {
              await outputFile.delete();
            } else {
              completedBytes += chunkedFile.fileSize;
              final percentage = completedBytes * 100 / totalBytes;
              if(percentage - lastPercentage > 0.1) {
                _onProgress(
                    startTime,
                    DateTime.now().millisecondsSinceEpoch,
                    percentage,
                    false,
                    options
                );
              }
              return;
            }
          }

          await outputFile.parent.create(recursive: true);
          for(final chunkId in chunkedFile.chunksIds) {
            final response = await _dio.get<Uint8List>(
              "$_manifestSourceUrl/${options.build.identifier}/$chunkId.chunk",
              options: Options(
                  responseType: ResponseType.bytes,
                  headers: {
                    "Accept-Encoding": "gzip"
                  }
              ),
            );
            var responseBody = response.data;
            if(responseBody == null) {
              continue;
            }

            final decodedBody = codec.decode(responseBody);
            await outputFile.writeAsBytes(
                decodedBody,
                mode: FileMode.append,
                flush: true
            );
            completedBytes += decodedBody.length;

            final percentage = completedBytes * 100 / totalBytes;
            if(percentage - lastPercentage > 0.1) {
              _onProgress(
                  startTime,
                  DateTime.now().millisecondsSinceEpoch,
                  percentage,
                  false,
                  options
              );
            }
          }
        });
        await Future.any([stopped.future, Future.wait(writers)]);
        options.port.send(FortniteBuildDownloadProgress(100, 0, true));
        break;
    }
  }catch(error, stackTrace) {
    options.port.send("$error\n$stackTrace");
  }
}

Future<Response> _downloadArchive(FortniteBuildDownloadOptions options, File tempFile, int startTime, [int? byteStart = null, int errorsCount = 0]) async {
  var received = byteStart ?? 0;
  try {
    return await _dio.download(
        options.build.link,
        tempFile.path,
        onReceiveProgress: (data, length) {
          received = data;
          final percentage = (received / length) * 100;
          _onProgress(startTime, DateTime.now().millisecondsSinceEpoch, percentage, false, options);
        },
        deleteOnError: false,
        options: Options(
            headers: byteStart == null ? null : {
              "Range": "bytes=${byteStart}-"
            }
        )
    );
  }catch(error) {
    if(errorsCount >= _maxDownloadErrors) {
      throw error;
    }

    return await _downloadArchive(options, tempFile, startTime, received, errorsCount + 1);
  }
}

Future<void> _extractArchive(Completer<dynamic> stopped, String extension, File tempFile, FortniteBuildDownloadOptions options) async {
  var startTime = DateTime.now().millisecondsSinceEpoch;
  Process? process;
  switch (extension.toLowerCase()) {
    case ".zip":
      process = await Process.start(
          "${assetsDirectory.path}\\build\\7zip.exe",
          ["a", "-bsp1", '-o"${options.destination.path}"', tempFile.path]
      );
      process.stdout.listen((bytes) {
        final now = DateTime.now().millisecondsSinceEpoch;
        final data = utf8.decode(bytes);
        if(data == "Everything is Ok") {
          options.port.send(FortniteBuildDownloadProgress(100, 0, true));
          return;
        }

        final element = data.trim().split(" ")[0];
        if(!element.endsWith("%")) {
          return;
        }

        final percentage = int.parse(element.substring(0, element.length - 1)).toDouble();
        _onProgress(startTime, now, percentage, true, options);
      });
      break;
    case ".rar":
      process = await Process.start(
          "${assetsDirectory.path}\\build\\winrar.exe",
          ["x", "-o+", tempFile.path, "*.*", options.destination.path]
      );
      process.stdout.listen((event) {
        final now = DateTime.now().millisecondsSinceEpoch;
        final data = utf8.decode(event);
        data.replaceAll("\r", "")
            .replaceAll("\b", "")
            .trim()
            .split("\n")
            .forEach((entry) {
          if(entry == "All OK") {
            options.port.send(FortniteBuildDownloadProgress(100, 0, true));
            return;
          }

          final element = _rarProgressRegex.firstMatch(entry)?.group(1);
          if(element == null) {
            return;
          }

          final percentage = int.parse(element).toDouble();
          _onProgress(startTime, now, percentage, true, options);
        });
      });
      process.stderr.listen((event) {
        final data = utf8.decode(event);
        options.port.send(data);
      });
      break;
    default:
      throw ArgumentError("Unexpected file extension: $extension}");
  }

  await Future.any([stopped.future, process.exitCode]);
}

void _onProgress(int startTime, int now, double percentage, bool extracting, FortniteBuildDownloadOptions options) {
  if(percentage == 0) {
    options.port.send(FortniteBuildDownloadProgress(percentage, null, extracting));
    return;
  }

  final msLeft = startTime + (now - startTime) * 100 / percentage - now;
  final minutesLeft = (msLeft / 1000 / 60).round();
  options.port.send(FortniteBuildDownloadProgress(percentage, minutesLeft, extracting));
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