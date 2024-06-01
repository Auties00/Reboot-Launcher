import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:isolate';
import 'dart:typed_data';

import 'package:dio/dio.dart';
import 'package:dio/io.dart';
import 'package:path/path.dart' as path;
import 'package:reboot_common/common.dart';

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
const String _manifestSourceUrl = "http://manifest.simplyblk.xyz";
const String _deniedConnectionError = "The connection was denied: your firewall might be blocking the download";
const String _unavailableError = "The build downloader is not available right now";
const String _genericError = "The build downloader is not working correctly";
const int _maxErrors = 100;

Future<List<FortniteBuild>> fetchBuilds(ignored) async {
  (_dio.httpClientAdapter as IOHttpClientAdapter).createHttpClient = () =>
  HttpClient()
    ..badCertificateCallback =
        (X509Certificate cert, String host, int port) => true;

  final results = await Future.wait([_fetchManifestBuilds(), _fetchArchiveBuilds()]);
  final data = <FortniteBuild>[];
  for(final result in results) {
    data.addAll(result);
  }

  return data;
}

Future<List<FortniteBuild>> _fetchManifestBuilds() async {
  try {
    final response = await _dio.get<String>(
        "$_manifestSourceUrl/versions.json",
        options: Options(
            headers: {
              "Accept-Encoding": "*",
              "Cookie": "_c_t_c=1"
            }
        )
    );
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
          await _extractArchive(stopped, extension, tempFile, options);
        }

        delete(outputDir);
        break;
      case FortniteBuildSource.manifest:
        final response = await _dio.get<String>(
            options.build.link,
          options: Options(
            headers: {
              "Cookie": "_c_t_c=1"
            }
          )
        );
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
                    "Accept-Encoding": "gzip",
                    "Cookie": "_c_t_c=1"
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
        break;
    }
  }catch(error) {
    _onError(error, options);
  }
}

Future<void> _downloadArchive(FortniteBuildDownloadOptions options, File tempFile, int startTime, [int? byteStart = null, int errorsCount = 0]) async {
  var received = byteStart ?? 0;
  try {
    await _dio.download(
        options.build.link,
        tempFile.path,
        onReceiveProgress: (data, length) {
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
    if(errorsCount > _maxErrors || error.toString().contains(_deniedConnectionError) || error.toString().contains(_unavailableError)) {
      _onError(error, options);
      return;
    }

    await _downloadArchive(options, tempFile, startTime, received, errorsCount + 1);
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
      process.stdOutput.listen((data) {
        final now = DateTime.now().millisecondsSinceEpoch;
        if(data.toLowerCase().contains("everything is ok")) {
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
      process.stdOutput.listen((data) {
        final now = DateTime.now().millisecondsSinceEpoch;
        data = data.replaceAll("\r", "").replaceAll("\b", "").trim();
        if(data == "All OK") {
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
      break;
    default:
      throw ArgumentError("Unexpected file extension: $extension}");
  }

  await Future.any([stopped.future, watchProcess(process.pid)]);
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