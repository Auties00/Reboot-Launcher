import 'dart:io';
import 'dart:isolate';

import 'package:version/version.dart';

class FortniteBuild {
  final Version version;
  final String link;
  final bool available;

  FortniteBuild({
    required this.version,
    required this.link,
    required this.available
  });
}

class FortniteBuildDownloadProgress {
  final double progress;
  final int? minutesLeft;
  final bool extracting;

  FortniteBuildDownloadProgress({
    required this.progress,
    required this.extracting,
    this.minutesLeft,
  });
}

class FortniteBuildDownloadOptions {
  FortniteBuild build;
  Directory destination;
  SendPort port;

  FortniteBuildDownloadOptions(this.build, this.destination, this.port);
}

class FortniteBuildManifestChunk {
  List<int> chunksIds;
  String file;
  int fileSize;

  FortniteBuildManifestChunk._internal(this.chunksIds, this.file, this.fileSize);

  factory FortniteBuildManifestChunk.fromJson(json) => FortniteBuildManifestChunk._internal(
      List<int>.from(json["ChunksIds"] as List),
      json["File"],
      json["FileSize"]
  );
}

class FortniteBuildManifestFile {
  String name;
  List<FortniteBuildManifestChunk> chunks;
  int size;

  FortniteBuildManifestFile._internal(this.name, this.chunks, this.size);

  factory FortniteBuildManifestFile.fromJson(json) => FortniteBuildManifestFile._internal(
      json["Name"],
      List<FortniteBuildManifestChunk>.from(json["Chunks"].map((chunk) => FortniteBuildManifestChunk.fromJson(chunk))),
      json["Size"]
  );
}
