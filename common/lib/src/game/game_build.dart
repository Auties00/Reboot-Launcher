import 'dart:io';
import 'dart:isolate';

class GameBuild {
  final String gameVersion;
  final String link;
  final bool available;

  GameBuild({
    required this.gameVersion,
    required this.link,
    required this.available
  });
}

class GameBuildDownloadProgress {
  final double progress;
  final int? timeLeft;
  final bool extracting;
  final int speed;

  GameBuildDownloadProgress({
    required this.progress,
    required this.extracting,
    required this.timeLeft,
    required this.speed
  });
}

class GameBuildDownloadOptions {
  GameBuild build;
  Directory destination;
  SendPort port;

  GameBuildDownloadOptions(this.build, this.destination, this.port);
}

class GameBuildManifestChunk {
  List<int> chunksIds;
  String file;
  int fileSize;

  GameBuildManifestChunk._internal(this.chunksIds, this.file, this.fileSize);

  factory GameBuildManifestChunk.fromJson(json) => GameBuildManifestChunk._internal(
      List<int>.from(json["ChunksIds"] as List),
      json["File"],
      json["FileSize"]
  );
}

class GameBuildManifestFile {
  String name;
  List<GameBuildManifestChunk> chunks;
  int size;

  GameBuildManifestFile._internal(this.name, this.chunks, this.size);

  factory GameBuildManifestFile.fromJson(json) => GameBuildManifestFile._internal(
      json["Name"],
      List<GameBuildManifestChunk>.from(json["Chunks"].map((chunk) => GameBuildManifestChunk.fromJson(chunk))),
      json["Size"]
  );
}
