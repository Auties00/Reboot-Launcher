import 'package:version/version.dart';

class RebootDownload {
  final int updateTime;
  final Object? error;
  final StackTrace? stackTrace;

  RebootDownload(this.updateTime, [this.error, this.stackTrace]);

  bool get hasError => error != null;
}
