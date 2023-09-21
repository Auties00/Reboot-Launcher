import 'dart:io';
import 'dart:isolate';

class ArchiveDownloadProgress {
  final double progress;
  final int? minutesLeft;
  final bool extracting;

  ArchiveDownloadProgress(this.progress, this.minutesLeft, this.extracting);
}

class ArchiveDownloadOptions {
  String archiveUrl;
  Directory destination;
  SendPort port;

  ArchiveDownloadOptions(this.archiveUrl, this.destination, this.port);
}
