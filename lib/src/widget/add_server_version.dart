import 'dart:async';
import 'dart:io';

import 'package:async/async.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/build_controller.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/model/fortnite_version.dart';
import 'package:reboot_launcher/src/util/binary.dart';
import 'package:reboot_launcher/src/util/build.dart';
import 'package:reboot_launcher/src/widget/select_file.dart';
import 'package:reboot_launcher/src/widget/version_name_input.dart';

import 'build_selector.dart';

class AddServerVersion extends StatefulWidget {
  const AddServerVersion(
      {Key? key})
      : super(key: key);

  @override
  State<AddServerVersion> createState() => _AddServerVersionState();
}

class _AddServerVersionState extends State<AddServerVersion> {
  final GameController _gameController = Get.find<GameController>();
  final BuildController _buildController = Get.find<BuildController>();
  final TextEditingController _nameController = TextEditingController();
  final TextEditingController _pathController = TextEditingController();
  late Future _future;
  DownloadStatus _status = DownloadStatus.none;
  double _downloadProgress = 0;
  DateTime? _downloadStartTime;
  DateTime? _lastUpdateTime;
  Duration? _lastUpdateTimeLeft;
  String? _lastUpdateTimeFormatted;
  String? _error;
  Process? _manifestDownloadProcess;
  CancelableOperation? _driveDownloadOperation;

  @override
  void initState() {
    _future = _buildController.builds != null
        ? Future.value(true)
        : compute(fetchBuilds, null)
        .then((value) => _buildController.builds = value);
    super.initState();
  }

  @override
  void dispose() {
    _pathController.dispose();
    _nameController.dispose();
    _onDisposed();
    super.dispose();
  }

  void _onDisposed() {
    if(_status != DownloadStatus.downloading && _status != DownloadStatus.extracting){
      return;
    }

    if (_manifestDownloadProcess != null) {
      loadBinary("stop.bat", false)
          .then((value) => Process.runSync(value.path, [])); // kill doesn't work :/
      _buildController.cancelledDownload.value = true;
      return;
    }

    if(_driveDownloadOperation == null){
      return;
    }

    _driveDownloadOperation!.cancel();
    _buildController.cancelledDownload.value = true;
  }

  @override
  Widget build(BuildContext context) {
    return Form(
        child: Builder(
            builder: (context) => ContentDialog(
                style: const ContentDialogThemeData(
                    padding: EdgeInsets.only(left: 20, right: 20, top: 15.0, bottom: 5.0)
                ),
                constraints: const BoxConstraints(maxWidth: 368, maxHeight: 321),
                content: _createDownloadVersionBody(),
                actions: _createDownloadVersionOption(context))));
  }

  List<Widget> _createDownloadVersionOption(BuildContext context) {
    switch (_status) {
      case DownloadStatus.none:
        return [
          Button(
              onPressed: () => _onClose(),
              child: const Text('Close')),
          FilledButton(
            onPressed: () => _startDownload(context),
            child: const Text('Download'),
          )
        ];

      case DownloadStatus.error:
        return [
          SizedBox(
              width: double.infinity,
              child: Button(
                  onPressed: () => _onClose(),
                  child: const Text('Close')))
        ];
      default:
        return [
          SizedBox(
            width: double.infinity,
            child: Button(
                onPressed: () => _onClose(),
                child: Text(
                    _status == DownloadStatus.downloading ? 'Stop' : 'Close')),
          )
        ];
    }
  }

  void _onClose() {
    Navigator.of(context).pop();
  }

  void _startDownload(BuildContext context) async {
    if (!Form.of(context)!.validate()) {
      return;
    }

    try {
      setState(() => _status = DownloadStatus.downloading);
      if (_buildController.selectedBuild.hasManifest) {
        _manifestDownloadProcess = await downloadManifestBuild(
            _buildController.selectedBuild.link, _pathController.text, _onDownloadProgress);
        _manifestDownloadProcess!.exitCode.then((value) => _onDownloadComplete());
      } else {
        _driveDownloadOperation = CancelableOperation.fromFuture(
                downloadArchiveBuild(_buildController.selectedBuild.link, _pathController.text,
                    _onDownloadProgress, _onUnrar))
            .then((_) => _onDownloadComplete(),
                onError: (error, _) => _handleError(error));
      }
    } catch (exception) {
      _handleError(exception);
    }
  }

  FutureOr? _handleError(Object exception) {
    var message = exception.toString();
    _onDownloadError(message.contains(":")
        ? " ${message.substring(message.indexOf(":") + 1)}"
        : message);
    return null;
  }

  void _onUnrar() {
    setState(() => _status = DownloadStatus.extracting);
  }

  void _onDownloadComplete() {
    if (!mounted) {
      return;
    }

    setState(() {
      _status = DownloadStatus.done;
      _gameController.addVersion(FortniteVersion(
          name: _nameController.text,
          location: Directory(_pathController.text)));
    });
  }

  void _onDownloadError(String message) {
    if (!mounted) {
      return;
    }

    setState(() {
      _status = DownloadStatus.error;
      _error = message;
    });
  }

  void _onDownloadProgress(double progress) {
    if (!mounted) {
      return;
    }

    _downloadStartTime ??= DateTime.now();
    setState(() {
      _status = DownloadStatus.downloading;
      _downloadProgress = progress;
    });
  }

  Widget _createDownloadVersionBody() {
    return FutureBuilder(
        future: _future,
        builder: (context, snapshot) {
          if (snapshot.hasError) {
            WidgetsBinding.instance.addPostFrameCallback((_) =>
                setState(() => _status = DownloadStatus.error));
            return Container(
              width: double.infinity,
              padding: const EdgeInsets.only(bottom: 16.0),
              child: Text("Cannot fetch builds: ${snapshot.error}",
                  textAlign: TextAlign.center),
            );
          }

          if (!snapshot.hasData) {
            return InfoLabel(
              label: "Fetching builds...",
              child: Container(
                  padding: const EdgeInsets.only(bottom: 16.0),
                  width: double.infinity,
                  child: const ProgressBar()
              ),
            );
          }

          return _buildBody();
        });
  }

  Widget _buildBody() {
    switch (_status) {
      case DownloadStatus.none:
        return Column(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const BuildSelector(),

            const SizedBox(height: 16.0),

            VersionNameInput(controller: _nameController),

            SelectFile(
                label: "Destination",
                placeholder: "Type the download destination",
                windowTitle: "Select download destination",
                controller: _pathController,
                validator: _checkDownloadDestination
            ),
          ],
        );
      case DownloadStatus.downloading:
        var timeLeft = _timeLeft;
        return Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Align(
              alignment: Alignment.centerLeft,
              child: Text(
                "Downloading...",
                style: FluentTheme.maybeOf(context)?.typography.body,
                textAlign: TextAlign.start,
              ),
            ),

            const SizedBox(
              height: 8,
            ),

            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                Text(
                  "${_downloadProgress.round()}%",
                  style: FluentTheme.maybeOf(context)?.typography.body,
                ),

                Text(
                  "Time left: ${timeLeft ?? "00:00:00"}",
                  style: FluentTheme.maybeOf(context)?.typography.body,
                ),
              ],
            ),

            const SizedBox(
              height: 8,
            ),

            SizedBox(
                width: double.infinity,
                child: ProgressBar(value: _downloadProgress.toDouble())
            ),

            const SizedBox(
              height: 16,
            )
          ],
        );
      case DownloadStatus.extracting:
        return const Padding(
          padding: EdgeInsets.only(bottom: 16),
          child: InfoLabel(
            label: "Extracting...",
            child: SizedBox(width: double.infinity, child: ProgressBar())
          ),
        );
      case DownloadStatus.done:
        return const Padding(
          padding: EdgeInsets.only(bottom: 16),
          child: SizedBox(
              width: double.infinity,
              child: Text("The download was completed successfully!",
                  textAlign: TextAlign.center)),
        );
      case DownloadStatus.error:
        return Padding(
          padding: const EdgeInsets.only(bottom: 16),
          child: SizedBox(
              width: double.infinity,
              child: Text(
                  "An error was occurred while downloading:$_error",
                  textAlign: TextAlign.center)),
        );
    }
  }

  String? get _timeLeft {
    if(_downloadStartTime == null){
      return null;
    }

    var now = DateTime.now();
    var elapsed = now.difference(_downloadStartTime!);
    var msLeft = (elapsed.inMilliseconds * 100) / _downloadProgress;
    if(!msLeft.isFinite){
      return null;
    }

    var timeLeft = Duration(milliseconds: msLeft.round() - elapsed.inMilliseconds);
    var delta = _lastUpdateTime == null || _lastUpdateTimeLeft == null ? -1
        : timeLeft.inMilliseconds - _lastUpdateTimeLeft!.inMilliseconds;
    var shouldSkip = delta == -1 || now.difference(_lastUpdateTime!).inMilliseconds > delta.abs() * 3;
    _lastUpdateTime = now;
    _lastUpdateTimeLeft = timeLeft;
    if(shouldSkip){
      return _lastUpdateTimeFormatted;
    }

    var twoDigitMinutes = _twoDigits(timeLeft.inMinutes.remainder(60));
    var twoDigitSeconds = _twoDigits(timeLeft.inSeconds.remainder(60));
    return _lastUpdateTimeFormatted =
            "${_twoDigits(timeLeft.inHours)}:$twoDigitMinutes:$twoDigitSeconds";
  }

  String _twoDigits(int n) => n.toString().padLeft(2, "0");

  String? _checkDownloadDestination(text) {
    if (text == null || text.isEmpty) {
      return 'Invalid download path';
    }

    return null;
  }
}

enum DownloadStatus { none, downloading, extracting, error, done }
