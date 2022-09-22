import 'dart:async';
import 'dart:io';
import 'package:async/async.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:get/get.dart';
import 'package:process_run/shell.dart';
import 'package:reboot_launcher/src/controller/build_controller.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/util/build.dart';
import 'package:reboot_launcher/src/util/binary.dart';
import 'package:reboot_launcher/src/widget/select_file.dart';
import 'package:reboot_launcher/src/widget/version_name_input.dart';

import 'package:reboot_launcher/src/model/fortnite_version.dart';
import '../model/fortnite_build.dart';
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
      _onCancelDownload();
      return;
    }

    if(_driveDownloadOperation == null){
      return;
    }

    _driveDownloadOperation!.cancel();
    _onCancelDownload();
  }

  void _onCancelDownload() {
       WidgetsBinding.instance.addPostFrameCallback((_) =>
        showSnackbar(context,
            const Snackbar(content: Text("Download cancelled"))));
  }

  @override
  Widget build(BuildContext context) {
    return Form(
        child: Builder(
            builder: (context) => ContentDialog(
                constraints:
                    const BoxConstraints(maxWidth: 368, maxHeight: 338),
                content: _createDownloadVersionBody(),
                actions: _createDownloadVersionOption(context))));
  }

  List<Widget> _createDownloadVersionOption(BuildContext context) {
    switch (_status) {
      case DownloadStatus.none:
        return [
          FilledButton(
              onPressed: () => _onClose(),
              style: ButtonStyle(backgroundColor: ButtonState.all(Colors.red)),
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
              child: FilledButton(
                  onPressed: () => _onClose(),
                  style:
                      ButtonStyle(backgroundColor: ButtonState.all(Colors.red)),
                  child: const Text('Close')))
        ];
      default:
        return [
          SizedBox(
            width: double.infinity,
            child: FilledButton(
                onPressed: () => _onClose(),
                style:
                    ButtonStyle(backgroundColor: ButtonState.all(Colors.red)),
                child: Text(
                    _status == DownloadStatus.downloading ? 'Stop' : 'Close')),
          )
        ];
    }
  }

  void _onClose() {
    Navigator.of(context).pop(true);
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
            snapshot.printError();
            return Text("Cannot fetch builds: ${snapshot.error}",
                textAlign: TextAlign.center);
          }

          if (!snapshot.hasData) {
            return const InfoLabel(
              label: "Fetching builds...",
              child: SizedBox(
                  width: double.infinity, child: ProgressBar()),
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
            VersionNameInput(controller: _nameController),
            SelectFile(
                label: "Destination",
                placeholder: "Type the download destination",
                windowTitle: "Select download destination",
                allowNavigator: false,
                controller: _pathController,
                validator: _checkDownloadDestination),
          ],
        );
      case DownloadStatus.downloading:
        return InfoLabel(
          label: "Downloading",
          child: InfoLabel(
              label: "${_downloadProgress.round()}%",
              child: SizedBox(
                  width: double.infinity,
                  child: ProgressBar(value: _downloadProgress.toDouble()))),
        );
      case DownloadStatus.extracting:
        return const InfoLabel(
          label: "Extracting",
          child: SizedBox(width: double.infinity, child: ProgressBar())
        );
      case DownloadStatus.done:
        return const SizedBox(
            width: double.infinity,
            child: Text("The download was completed successfully!",
                textAlign: TextAlign.center));
      case DownloadStatus.error:
        return SizedBox(
            width: double.infinity,
            child: Text(
                "An exception was thrown during the download process:$_error",
                textAlign: TextAlign.center));
    }
  }

  String? _checkDownloadDestination(text) {
    if (text == null || text.isEmpty) {
      return 'Invalid download path';
    }

    if (Directory(text).existsSync()) {
      return "Existent download destination";
    }

    return null;
  }
}

enum DownloadStatus { none, downloading, extracting, error, done }
