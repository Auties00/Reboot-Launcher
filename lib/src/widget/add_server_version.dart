import 'dart:async';
import 'dart:io';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/util/download_build.dart';
import 'package:reboot_launcher/src/util/locate_binary.dart';
import 'package:reboot_launcher/src/util/version_controller.dart';
import 'package:reboot_launcher/src/widget/select_file.dart';
import 'package:reboot_launcher/src/widget/version_name_input.dart';

import '../model/fortnite_build.dart';
import '../model/fortnite_version.dart';
import '../util/builds_scraper.dart';
import '../util/generic_controller.dart';
import 'build_selector.dart';

class AddServerVersion extends StatefulWidget {
  final VersionController controller;
  final Function onCancel;

  const AddServerVersion(
      {required this.controller, Key? key, required this.onCancel})
      : super(key: key);

  @override
  State<AddServerVersion> createState() => _AddServerVersionState();
}

class _AddServerVersionState extends State<AddServerVersion> {
  static List<FortniteBuild>? _builds;
  late GenericController<FortniteBuild?> _buildController;
  late TextEditingController _nameController;
  late TextEditingController _pathController;
  late DownloadStatus _status;
  late Future _future;
  double _downloadProgress = 0;
  String? _error;
  Process? _process;
  bool _disposed = false;

  @override
  void initState() {
    _future = _fetchBuilds();
    _buildController = GenericController(initialValue: null);
    _nameController = TextEditingController();
    _pathController = TextEditingController();
    _status = DownloadStatus.none;
    super.initState();
  }

  @override
  void dispose() {
    _disposed = true;
    _pathController.dispose();
    _nameController.dispose();
    if (_process != null && _status == DownloadStatus.downloading) {
      locateAndCopyBinary("stop.bat")
          .then((value) => Process.runSync(value, [])); // kill doesn't work :/
      widget.onCancel();
    }

    super.dispose();
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
      var build = _buildController.value!;
      if (build.hasManifest) {
        _process = await downloadManifestBuild(
            build.link, _pathController.text, _onDownloadProgress);
        _process!.exitCode.then((value) => _onDownloadComplete());
      } else {
        downloadArchiveBuild(
                build.link, _pathController.text, _onDownloadProgress, _onUnrar)
            .then((value) => _onDownloadComplete())
            .catchError(_handleError);
      }
    } catch (exception) {
      _handleError(exception);
    }
  }

  void _handleError(Object exception) {
    var message = exception.toString();
    _onDownloadError(message.contains(":")
        ? " ${message.substring(message.indexOf(":") + 1)}"
        : message);
  }

  void _onUnrar() {
    setState(() => _status = DownloadStatus.extracting);
  }

  void _onDownloadComplete() {
    if (_disposed) {
      return;
    }

    setState(() {
      _status = DownloadStatus.done;
      widget.controller.add(FortniteVersion(
          name: _nameController.text,
          location: Directory(_pathController.text)));
    });
  }

  void _onDownloadError(String message) {
    if (_disposed) {
      return;
    }

    setState(() {
      _status = DownloadStatus.error;
      _error = message;
    });
  }

  void _onDownloadProgress(double progress) {
    if (_disposed) {
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
            return Text("Cannot fetch builds: ${snapshot.error}",
                textAlign: TextAlign.center);
          }

          if (!snapshot.hasData) {
            return const InfoLabel(
              label: "Fetching builds...",
              child: SizedBox(
                  height: 32, width: double.infinity, child: ProgressBar()),
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
            BuildSelector(builds: _builds!, controller: _buildController),
            VersionNameInput(
              controller: _nameController,
              versions: widget.controller.versions,
            ),
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
          child: InfoLabel(
            label: "This might take a while...",
            child: SizedBox(width: double.infinity, child: ProgressBar()),
          ),
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

  Future<bool> _fetchBuilds() async {
    if (_builds != null) {
      return false;
    }

    _builds = await fetchBuilds();
    return true;
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
