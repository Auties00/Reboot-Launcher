import 'dart:async';
import 'dart:io';

import 'package:async/async.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/build_controller.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/dialog/dialog_button.dart';
import 'package:reboot_launcher/src/model/fortnite_version.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/util/build.dart';
import 'package:reboot_launcher/src/widget/home/version_name_input.dart';

import '../util/checks.dart';
import '../widget/home/build_selector.dart';
import '../widget/os/file_selector.dart';
import 'dialog.dart';

class AddServerVersion extends StatefulWidget {
  const AddServerVersion({Key? key}) : super(key: key);

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
  String _timeLeft = "00:00:00";
  double _downloadProgress = 0;
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
    if (_status != DownloadStatus.downloading &&
        _status != DownloadStatus.extracting) {
      return;
    }

    if (_manifestDownloadProcess != null) {
      loadBinary("stop.bat", true).then(
          (value) => Process.runSync(value.path, []));
      _buildController.cancelledDownload(true);
      return;
    }

    if (_driveDownloadOperation == null) {
      return;
    }

    _driveDownloadOperation!.cancel();
    _buildController.cancelledDownload(true);
  }

  @override
  Widget build(BuildContext context) {
    return FormDialog(
        content: _createDownloadVersionBody(),
        buttons: _createDownloadVersionOption(context)
    );
  }

  List<DialogButton> _createDownloadVersionOption(BuildContext context) {
    switch (_status) {
      case DownloadStatus.none:
        return [
          DialogButton(type: ButtonType.secondary),
          DialogButton(
            text: "Download",
            type: ButtonType.primary,
            onTap: () => _startDownload(context),
          )
        ];

      case DownloadStatus.error:
        return [DialogButton(type: ButtonType.only)];
      default:
        return [
          DialogButton(
              text: _status == DownloadStatus.downloading ? "Stop" : "Close",
              type: ButtonType.only)
        ];
    }
  }

  void _startDownload(BuildContext context) async {
    try {
      setState(() => _status = DownloadStatus.downloading);
      if (_buildController.selectedBuild.hasManifest) {
        _manifestDownloadProcess = await downloadManifestBuild(
            _buildController.selectedBuild.link,
            _pathController.text,
            _onDownloadProgress
        );
        _manifestDownloadProcess!.exitCode
            .then((value) => _onDownloadComplete());
      } else {
        _driveDownloadOperation = CancelableOperation.fromFuture(
                downloadArchiveBuild(
                    _buildController.selectedBuild.link,
                    _pathController.text,
                    (progress) => _onDownloadProgress(progress, _timeLeft),
                    _onUnrar)
        ).then((_) => _onDownloadComplete(),
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

  void _onDownloadProgress(double progress, String timeLeft) {
    if (!mounted) {
      return;
    }

    setState(() {
      _status = DownloadStatus.downloading;
      _timeLeft = timeLeft;
      _downloadProgress = progress;
    });
  }

  Widget _createDownloadVersionBody() {
    return FutureBuilder(
        future: _future,
        builder: (context, snapshot) {
          if (snapshot.hasError) {
            WidgetsBinding.instance.addPostFrameCallback(
                (_) => setState(() => _status = DownloadStatus.error));
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
                  child: const ProgressBar()),
            );
          }

          return _buildBody();
        });
  }

  Widget _buildBody() {
    switch (_status) {
      case DownloadStatus.none:
        return _createNoneBody();
      case DownloadStatus.downloading:
        return _createDownloadBody();
      case DownloadStatus.extracting:
        return _createExtractingBody();
      case DownloadStatus.done:
        return _createDoneBody();
      case DownloadStatus.error:
        return _createErrorBody();
    }
  }

  Padding _createErrorBody() {
    return Padding(
      padding: const EdgeInsets.only(bottom: 16),
      child: SizedBox(
          width: double.infinity,
          child: Text("An error was occurred while downloading:$_error",
              textAlign: TextAlign.center)),
    );
  }

  Padding _createDoneBody() {
    return const Padding(
      padding: EdgeInsets.only(bottom: 16),
      child: SizedBox(
          width: double.infinity,
          child: Text("The download was completed successfully!",
              textAlign: TextAlign.center)),
    );
  }

  Padding _createExtractingBody() {
    return Padding(
      padding: const EdgeInsets.only(bottom: 16),
      child: InfoLabel(
          label: "Extracting...",
          child: const SizedBox(width: double.infinity, child: ProgressBar())),
    );
  }

  Column _createDownloadBody() {
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
        if(_manifestDownloadProcess != null)
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Text(
                "${_downloadProgress.round()}%",
                style: FluentTheme.maybeOf(context)?.typography.body,
              ),
              Text(
                "Time left: $_timeLeft",
                style: FluentTheme.maybeOf(context)?.typography.body,
              ),
            ],
          ),
        if(_manifestDownloadProcess != null)
          const SizedBox(
            height: 8,
          ),
        SizedBox(
            width: double.infinity,
            child: ProgressBar(value: _downloadProgress.toDouble())),
        const SizedBox(
          height: 16,
        )
      ],
    );
  }

  Column _createNoneBody() {
    return Column(
      mainAxisSize: MainAxisSize.min,
      mainAxisAlignment: MainAxisAlignment.spaceBetween,
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const BuildSelector(),
        const SizedBox(height: 16.0),
        VersionNameInput(controller: _nameController),
        const SizedBox(height: 16.0),
        FileSelector(
            label: "Destination",
            placeholder: "Type the download destination",
            windowTitle: "Select download destination",
            controller: _pathController,
            validator: checkDownloadDestination,
            folder: true),
        const SizedBox(height: 8.0),
      ],
    );
  }
}

enum DownloadStatus { none, downloading, extracting, error, done }
