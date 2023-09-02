import 'dart:async';
import 'dart:io';
import 'dart:isolate';

import 'package:http/http.dart' as http;
import 'package:path/path.dart' as path;
import 'package:reboot_common/common.dart';

final Uri _manifestSourceUrl = Uri.parse(
    "https://raw.githubusercontent.com/simplyblk/Fortnitebuilds/main/README.md");

Future<List<FortniteBuild>> fetchBuilds(ignored) async {
  var response = await http.get(_manifestSourceUrl);
  if (response.statusCode != 200) {
    throw Exception("Erroneous status code: ${response.statusCode}");
  }

  var results = <FortniteBuild>[];
  for (var line in response.body.split("\n")) {
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
  try {
    options.destination.createSync(recursive: true);
    var fileName = options.archiveUrl.substring(options.archiveUrl.lastIndexOf("/") + 1);
    var extension = path.extension(fileName);
    var tempFile = File("${outputDir.path}\\$fileName");
    if(tempFile.existsSync()) {
      tempFile.deleteSync(recursive: true);
    }

    await _download(options, tempFile, stopped);
    await _extract(stopped, extension, tempFile, options);
    delete(outputDir);
  } catch(message) {
    throw Exception("Cannot download build: $message");
  }
}

Future<void> _download(ArchiveDownloadOptions options, File tempFile, Completer<dynamic> stopped) async {
  var client = http.Client();
  var request = http.Request("GET", Uri.parse(options.archiveUrl));
  request.headers['Connection'] = 'Keep-Alive';
  var response = await client.send(request);
  if (response.statusCode != 200) {
    throw Exception("Erroneous status code: ${response.statusCode}");
  }

  var startTime = DateTime.now().millisecondsSinceEpoch;
  var length = response.contentLength!;
  var received = 0;
  var sink = tempFile.openWrite();
  var subscription = response.stream.listen((data) async {
    received += data.length;
    var now = DateTime.now();
    var progress = (received / length) * 100;
    var msLeft = startTime + (now.millisecondsSinceEpoch - startTime) * length / received - now.millisecondsSinceEpoch;
    var minutesLeft = (msLeft / 1000 / 60).round();
    options.port.send(ArchiveDownloadProgress(progress, minutesLeft, false));
    sink.add(data);
  });

  await Future.any([stopped.future, subscription.asFuture()]);
  if(stopped.isCompleted) {
    await subscription.cancel();
  }else {
    await sink.flush();
    await sink.close();
    await sink.done;
  }
}

Future<void> _extract(Completer<dynamic> stopped, String extension, File tempFile, ArchiveDownloadOptions options) async {
  if(stopped.isCompleted) {
    return;
  }

  options.port.send(ArchiveDownloadProgress(0, -1, true));
  Process? process;
  switch (extension.toLowerCase()) {
    case '.zip':
      process = await Process.start(
          'tar',
          ['-xf', tempFile.path, '-C', options.destination.path],
          mode: ProcessStartMode.inheritStdio
      );
      break;
    case '.rar':
      process = await Process.start(
          '${assetsDirectory.path}\\misc\\winrar.exe',
          ['x', tempFile.path, '*.*', options.destination.path],
          mode: ProcessStartMode.inheritStdio
      );
      break;
    default:
      throw ArgumentError("Unexpected file extension: $extension}");
  }

  await Future.any([stopped.future, process.exitCode]);
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

class ArchiveDownloadOptions {
  String archiveUrl;
  Directory destination;
  SendPort port;

  ArchiveDownloadOptions(this.archiveUrl, this.destination, this.port);
}

class ArchiveDownloadProgress {
  final double progress;
  final int minutesLeft;
  final bool extracting;

  ArchiveDownloadProgress(this.progress, this.minutesLeft, this.extracting);
}