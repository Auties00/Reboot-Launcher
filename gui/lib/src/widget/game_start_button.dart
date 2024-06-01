import 'dart:async';
import 'dart:io';

import 'package:async/async.dart';
import 'package:dart_ipify/dart_ipify.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:path/path.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/backend_controller.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/hosting_controller.dart';
import 'package:reboot_launcher/src/controller/matchmaker_controller.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/dialog/abstract/dialog.dart';
import 'package:reboot_launcher/src/dialog/abstract/dialog_button.dart';
import 'package:reboot_launcher/src/dialog/abstract/info_bar.dart';
import 'package:reboot_launcher/src/dialog/implementation/server.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/page/pages.dart';
import 'package:reboot_launcher/src/util/dll.dart';
import 'package:reboot_launcher/src/util/log.dart';
import 'package:reboot_launcher/src/util/matchmaker.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:url_launcher/url_launcher.dart';

class LaunchButton extends StatefulWidget {
  final bool host;
  final String? startLabel;
  final String? stopLabel;

  const LaunchButton({Key? key, required this.host, this.startLabel, this.stopLabel}) : super(key: key);

  @override
  State<LaunchButton> createState() => _LaunchButtonState();
}

class _LaunchButtonState extends State<LaunchButton> {
  final GameController _gameController = Get.find<GameController>();
  final HostingController _hostingController = Get.find<HostingController>();
  final BackendController _backendController = Get.find<BackendController>();
  final MatchmakerController _matchmakerController = Get.find<MatchmakerController>();
  final SettingsController _settingsController = Get.find<SettingsController>();
  InfoBarEntry? _gameClientInfoBar;
  InfoBarEntry? _gameServerInfoBar;
  CancelableOperation? _operation;
  IVirtualDesktop? _virtualDesktop;

  @override
  Widget build(BuildContext context) => Align(
    alignment: AlignmentDirectional.bottomCenter,
    child: SizedBox(
      width: double.infinity,
      child: Obx(() => SizedBox(
        height: 48,
        child: Button(
            onPressed: () => _operation = CancelableOperation.fromFuture(_toggle()),
            child: Align(
                alignment: Alignment.center,
                child: Text(_hasStarted ? _stopMessage : _startMessage)
            )
        ),
      )),
    ),
  );

  bool get _hasStarted => widget.host ? _hostingController.started() : _gameController.started();

  void _setStarted(bool hosting, bool started) => hosting ? _hostingController.started.value = started : _gameController.started.value = started;

  String get _startMessage => widget.startLabel ?? (widget.host ? translations.startHosting : translations.startGame);

  String get _stopMessage => widget.stopLabel ?? (widget.host ? translations.stopHosting : translations.stopGame);

  Future<void> _toggle({bool forceGUI = false}) async {
    log("[${widget.host ? 'HOST' : 'GAME'}] Toggling state(forceGUI: $forceGUI)");
    if (_hasStarted) {
      log("[${widget.host ? 'HOST' : 'GAME'}] User asked to close the current instance");
      _onStop(
          reason: _StopReason.normal
      );
      return;
    }

    if(_operation != null) {
      log("[${widget.host ? 'HOST' : 'GAME'}] Already started, ignoring user action");
      return;
    }

    final version = _gameController.selectedVersion;
    log("[${widget.host ? 'HOST' : 'GAME'}] Version data: $version");
    if(version == null){
      log("[${widget.host ? 'HOST' : 'GAME'}] No version selected");
      _onStop(
          reason: _StopReason.missingVersionError
      );
      return;
    }

    log("[${widget.host ? 'HOST' : 'GAME'}] Setting started...");
    _setStarted(widget.host, true);
    log("[${widget.host ? 'HOST' : 'GAME'}] Set started");
    log("[${widget.host ? 'HOST' : 'GAME'}] Checking dlls: ${_Injectable.values}");
    for (final injectable in _Injectable.values) {
      if(await _getDllFileOrStop(injectable, widget.host) == null) {
        return;
      }
    }

    try {
      final executable = version.gameExecutable;
      if(executable == null){
        log("[${widget.host ? 'HOST' : 'GAME'}] No executable found");
        _onStop(
            reason: _StopReason.missingExecutableError,
            error: version.location.path
        );
        return;
      }

      log("[${widget.host ? 'HOST' : 'GAME'}] Checking backend(port: ${_backendController.type.value.name}, type: ${_backendController.type.value.name})...");
      final backendResult = _backendController.started() || await _backendController.toggleInteractive();
      if(!backendResult){
        log("[${widget.host ? 'HOST' : 'GAME'}] Cannot start backend");
        _onStop(
            reason: _StopReason.backendError
        );
        return;
      }
      log("[${widget.host ? 'HOST' : 'GAME'}] Backend works");

      log("[${widget.host ? 'HOST' : 'GAME'}] Checking matchmaker(port: ${_matchmakerController.type.value.name}, type: ${_matchmakerController.type.value.name})...");
      final matchmakerResult = _matchmakerController.started() || await _matchmakerController.toggleInteractive();
      if(!matchmakerResult){
        log("[${widget.host ? 'HOST' : 'GAME'}] Cannot start matchmaker");
        _onStop(
            reason: _StopReason.matchmakerError
        );
        return;
      }
      log("[${widget.host ? 'HOST' : 'GAME'}] Matchmaker works");

      final headless = !forceGUI && _hostingController.headless.value;
      final virtualDesktop = _hostingController.virtualDesktop.value;
      log("[${widget.host ? 'HOST' : 'GAME'}] Implicit game server metadata: headless($headless)");
      final linkedHostingInstance = await _startMatchMakingServer(version, headless, virtualDesktop, false);
      log("[${widget.host ? 'HOST' : 'GAME'}] Implicit game server result: $linkedHostingInstance");
      await _startGameProcesses(version, widget.host, headless, virtualDesktop, linkedHostingInstance);
      if(!widget.host) {
        _showLaunchingGameClientWidget();
      }

      if(linkedHostingInstance != null || widget.host){
        _showLaunchingGameServerWidget();
      }
    } catch (exception, stackTrace) {
      _onStop(
          reason: _StopReason.unknownError,
          error: exception.toString(),
          stackTrace: stackTrace
      );
    }
  }

  Future<GameInstance?> _startMatchMakingServer(FortniteVersion version, bool headless, bool virtualDesktop, bool forceLinkedHosting) async {
    log("[${widget.host ? 'HOST' : 'GAME'}] Checking if a server needs to be started automatically...");
    if(widget.host){
      log("[${widget.host ? 'HOST' : 'GAME'}] The user clicked on Start hosting, so it's not necessary");
      return null;
    }

    final matchmakingIp = _matchmakerController.gameServerAddress.text;
    if(!isLocalHost(matchmakingIp)) {
      log("[${widget.host ? 'HOST' : 'GAME'}] The current IP($matchmakingIp) is not set to localhost");
      return null;
    }

    if(_hostingController.started()){
      log("[${widget.host ? 'HOST' : 'GAME'}] The user has already manually started the hosting server");
      return null;
    }

    final response = forceLinkedHosting || await _askForAutomaticGameServer();
    if(!response) {
      log("[${widget.host ? 'HOST' : 'GAME'}] The user disabled the automatic server");
      return null;
    }

    log("[${widget.host ? 'HOST' : 'GAME'}] Starting implicit game server...");
    final instance = await _startGameProcesses(version, true, headless, virtualDesktop, null);
    log("[${widget.host ? 'HOST' : 'GAME'}] Started implicit game server...");
    _setStarted(true, true);
    log("[${widget.host ? 'HOST' : 'GAME'}] Set implicit game server as started");
    return instance;
  }

  Future<bool> _askForAutomaticGameServer() async {
    final result = await showAppDialog<bool>(
        builder: (context) => InfoDialog(
          text: "The launcher detected that you are not running a game server, but that your matchmaker is set to your local machine. "
              "If you don't want to join another player's server, you should start a game server. This is necessary to be able to play: for more information check the Info tab in the launcher.",
          buttons: [
            DialogButton(
                type: ButtonType.secondary,
                text: "Ignore"
            ),
            DialogButton(
              type: ButtonType.primary,
              text: "Start server",
              onTap: () => Navigator.of(context).pop(true),
            ),
          ],
        )
    ) ?? false;
    await Future.delayed(const Duration(seconds: 1));
    return result;
  }

  Future<GameInstance?> _startGameProcesses(FortniteVersion version, bool host, bool headless, bool virtualDesktop, GameInstance? linkedHosting) async {
    log("[${host ? 'HOST' : 'GAME'}] Starting game process...");
    log("[${host ? 'HOST' : 'GAME'}] Starting paused launcher...");
    final launcherProcess = await _createPausedProcess(version, version.launcherExecutable);

    log("[${host ? 'HOST' : 'GAME'}] Started paused launcher: $launcherProcess");
    log("[${host ? 'HOST' : 'GAME'}] Starting paused eac...");
    final eacProcess = await _createPausedProcess(version, version.eacExecutable);

    log("[${host ? 'HOST' : 'GAME'}] Started paused eac: $eacProcess");
    final executable = host && headless ? await version.headlessGameExecutable : version.gameExecutable;
    log("[${host ? 'HOST' : 'GAME'}] Using game path: ${executable?.path}");
    final gameProcess = await _createGameProcess(version, executable!, host, headless, virtualDesktop, linkedHosting);
    if(gameProcess == null) {
      log("[${host ? 'HOST' : 'GAME'}] No game process was created");
      return null;
    }

    log("[${host ? 'HOST' : 'GAME'}] Created game process: ${gameProcess}");
    final instance = GameInstance(
        versionName: version.name,
        gamePid: gameProcess,
        launcherPid: launcherProcess,
        eacPid: eacProcess,
        hosting: host,
        child: linkedHosting
    );
    if(host){
      _hostingController.discardServer();
      _hostingController.instance.value = instance;
    }else{
      _gameController.instance.value = instance;
    }
    await _injectOrShowError(_Injectable.sslBypassV2, host);
    log("[${host ? 'HOST' : 'GAME'}] Finished creating game instance");
    return instance;
  }

  Future<int?> _createGameProcess(FortniteVersion version, File executable, bool host, bool headless, bool virtualDesktop, GameInstance? linkedHosting) async {
    if(!_hasStarted) {
      log("[${host ? 'HOST' : 'GAME'}] Discarding start game process request as the state is no longer started");
      return null;
    }

    log("[${host ? 'HOST' : 'GAME'}] Generating instance args...");
    final gameArgs = createRebootArgs(
        _gameController.username.text,
        _gameController.password.text,
        host,
        _hostingController.headless.value,
        _gameController.customLaunchArgs.text
    );
    log("[${host ? 'HOST' : 'GAME'}] Generated game args: $gameArgs");
    final gameProcess = await startProcess(
        executable: executable,
        args: gameArgs,
        wrapProcess: false,
        name: "${version.name}-${host ? 'HOST' : 'GAME'}"
    );
    gameProcess.stdOutput.listen((line) => _onGameOutput(line, host, virtualDesktop, false));
    gameProcess.stdError.listen((line) => _onGameOutput(line, host, virtualDesktop, true));
    watchProcess(gameProcess.pid).then((_) async {
      final instance = host ? _hostingController.instance.value : _gameController.instance.value;
      if(instance == null || !host || !headless || instance.launched) {
        _onStop(reason: _StopReason.exitCode);
        return;
      }

      if(widget.host) {
        await _onStop(reason: _StopReason.exitCode);
        _toggle(forceGUI: true);
        return;
      }

      await _onStop(
          reason: _StopReason.exitCode,
          host: true
      );
      final linkedHostingInstance = await _startMatchMakingServer(version, false, virtualDesktop, true);
      _gameController.instance.value?.child = linkedHostingInstance;
      if(linkedHostingInstance != null){
        _setStarted(true, true);
        _showLaunchingGameServerWidget();
      }
    });
    if(host && !headless && virtualDesktop) {
      final name = version.name;
      final pid = gameProcess.pid;
      _moveProcessToVirtualDesktop(name, pid);
    }
    return gameProcess.pid;
  }

  Future<void> _moveProcessToVirtualDesktop(String versionName, int pid) async {
    try {
      final windowManager = VirtualDesktopManager.getInstance();
      _virtualDesktop = windowManager.createDesktop();
      windowManager.setDesktopName(_virtualDesktop!, "$versionName Server (Reboot Launcher)");
      Object? lastError;
      for(var i = 0; i < 10; i++) {
        try {
          windowManager.moveWindowToDesktop(pid, _virtualDesktop!);
          break;
        }catch(error) {
          lastError = error;
          await Future.delayed(Duration(seconds: 1));
        }
      }
      if(lastError != null) {
        log("[VIRTUAL_DESKTOP] Cannot move window: $lastError");
      }
    }catch(error) {
      log("[VIRTUAL_DESKTOP] Virtual desktop is not supported: $error");
    }
  }

  Future<int?> _createPausedProcess(FortniteVersion version, File? file) async {
    if (file == null) {
      return null;
    }

    final process = await startProcess(
        executable: file,
        wrapProcess: false,
        name: "${version.name}-${basenameWithoutExtension(file.path)}"
    );
    final pid = process.pid;
    suspend(pid);
    return pid;
  }

  void _onGameOutput(String line, bool host, bool virtualDesktop, bool error) async {
    if (line.contains(kShutdownLine)) {
      _onStop(
          reason: _StopReason.normal
      );
      return;
    }

    if(kCorruptedBuildErrors.any((element) => line.contains(element))){
      _onStop(
          reason: _StopReason.corruptedVersionError
      );
      return;
    }

    if(kCannotConnectErrors.any((element) => line.contains(element))){
      _onStop(
          reason: _StopReason.tokenError
      );
      return;
    }

    if(kLoggedInLines.every((entry) => line.contains(entry))) {
      await _injectOrShowError(_Injectable.memoryFix, host);
      if(!host){
        await _injectOrShowError(_Injectable.console, host);
        _onGameClientInjected();
      }else {
        await _injectOrShowError(_Injectable.reboot, host);
        _onGameServerInjected();
      }
      final instance = host ? _hostingController.instance.value : _gameController.instance.value;
      instance?.launched = true;
      instance?.tokenError = false;
    }
  }

  void _onGameClientInjected() {
    _gameClientInfoBar?.close();
    showInfoBar(
        translations.gameClientStarted,
        severity: InfoBarSeverity.success,
        duration: infoBarLongDuration
    );
  }

  Future<void> _onGameServerInjected() async {
    _gameServerInfoBar?.close();
    final theme = FluentTheme.of(appKey.currentContext!);
    try {
      _gameServerInfoBar = showInfoBar(
          translations.waitingForGameServer,
          loading: true,
          duration: null
      );
      final gameServerPort = _settingsController.gameServerPort.text;
      final localPingResult = await pingGameServer(
          "127.0.0.1:$gameServerPort",
          timeout: const Duration(minutes: 2)
      );
      if (!localPingResult) {
        _gameServerInfoBar?.close();
        showInfoBar(
            translations.gameServerStartWarning,
            severity: InfoBarSeverity.error,
            duration: infoBarLongDuration
        );
        return;
      }

      _matchmakerController.joinLocalHost();
      final accessible = await _checkGameServer(theme, gameServerPort);
      if (!accessible) {
        showInfoBar(
            translations.gameServerStartLocalWarning,
            severity: InfoBarSeverity.warning,
            duration: infoBarLongDuration
        );
        return;
      }

      await _hostingController.publishServer(
        _gameController.username.text,
        _hostingController.instance.value!.versionName,
      );
      showInfoBar(
          translations.gameServerStarted,
          severity: InfoBarSeverity.success,
          duration: infoBarLongDuration
      );
    }finally {
      _gameServerInfoBar?.close();
    }
  }

  Future<bool> _checkGameServer(FluentThemeData theme, String gameServerPort) async {
    try {
      _gameServerInfoBar?.close();
      _gameServerInfoBar = showInfoBar(
          translations.checkingGameServer,
          loading: true,
          duration: null
      );
      final publicIp = await Ipify.ipv4();
      final externalResult = await pingGameServer("$publicIp:$gameServerPort");
      if (externalResult) {
        return true;
      }

      _gameServerInfoBar?.close();
      final future = pingGameServer(
          "$publicIp:$gameServerPort",
          timeout: const Duration(days: 365)
      );
      _gameServerInfoBar = showInfoBar(
          translations.checkGameServerFixMessage(gameServerPort),
          action: Button(
            onPressed: () async {
              pageIndex.value = RebootPageType.info.index;
            },
            child: Text(translations.checkGameServerFixAction),
          ),
          severity: InfoBarSeverity.warning,
          duration: null,
          loading: true
      );
      return await future;
    }finally {
      _gameServerInfoBar?.close();
    }
  }

  Future<void> _onStop({required _StopReason reason, bool? host, String? error, StackTrace? stackTrace}) async {
    if(_virtualDesktop != null) {
      try {
        final instance = VirtualDesktopManager.getInstance();
        instance.removeDesktop(_virtualDesktop!);
      }catch(error) {
        log("[VIRTUAL_DESKTOP] Cannot close virtual desktop: $error");
      }
    }

    if(host == null) {
      await _operation?.cancel();
      _operation = null;
      await _backendController.worker?.cancel();
      await _matchmakerController.worker?.cancel();
    }

    host = host ?? widget.host;
    log("[${host ? 'HOST' : 'GAME'}] Called stop with reason $reason, error data $error $stackTrace");
    if(host) {
      _hostingController.discardServer();
    }

    final instance = host ? _hostingController.instance.value : _gameController.instance.value;
    if(instance != null) {
      if(reason == _StopReason.normal) {
        instance.launched = true;
      }

      instance.kill();
      final child = instance.child;
      if(child != null) {
        _onStop(
            reason: reason,
            host: child.hosting
        );
      }

      if(host){
        _hostingController.instance.value = null;
      }else {
        _gameController.instance.value = null;
      }
    }

    _setStarted(host, false);
    if(!reason.isError) {
      if(host) {
        _gameServerInfoBar?.close();
      }else {
        _gameClientInfoBar?.close();
      }
    }

    switch(reason) {
      case _StopReason.backendError:
      case _StopReason.matchmakerError:
      case _StopReason.normal:
        break;
      case _StopReason.missingVersionError:
        showInfoBar(
          translations.missingVersionError,
          severity: InfoBarSeverity.error,
          duration: infoBarLongDuration,
        );
        break;
      case _StopReason.missingExecutableError:
        showInfoBar(
          translations.missingExecutableError,
          severity: InfoBarSeverity.error,
          duration: infoBarLongDuration,
        );
        break;
      case _StopReason.exitCode:
        final instance = host ? _hostingController.instance.value : _gameController.instance.value;
        if(instance != null && !instance.launched) {
          showInfoBar(
            translations.corruptedVersionError,
            severity: InfoBarSeverity.error,
            duration: infoBarLongDuration,
          );
        }

        break;
      case _StopReason.corruptedVersionError:
        showInfoBar(
          translations.corruptedVersionError,
          severity: InfoBarSeverity.error,
          duration: infoBarLongDuration,
        );
        break;
      case _StopReason.corruptedDllError:
        showInfoBar(
          translations.corruptedDllError(error!),
          severity: InfoBarSeverity.error,
          duration: infoBarLongDuration,
        );
        break;
      case _StopReason.tokenError:
        showInfoBar(
          translations.tokenError,
          severity: InfoBarSeverity.error,
          duration: infoBarLongDuration,
        );
        break;
      case _StopReason.unknownError:
        showInfoBar(
          translations.unknownFortniteError(error ?? translations.unknownError),
          severity: InfoBarSeverity.error,
          duration: infoBarLongDuration,
        );
        break;
    }
  }

  Future<void> _injectOrShowError(_Injectable injectable, bool hosting) async {
    final instance = hosting ? _hostingController.instance.value : _gameController.instance.value;
    if (instance == null) {
      log("[${hosting ? 'HOST' : 'GAME'}] No instance found to inject ${injectable.name}");
      return;
    }

    try {
      final gameProcess = instance.gamePid;
      log("[${hosting ? 'HOST' : 'GAME'}] Injecting ${injectable.name} into process with pid $gameProcess");
      final dllPath = await _getDllFileOrStop(injectable, hosting);
      log("[${hosting ? 'HOST' : 'GAME'}] File to inject for ${injectable.name} at path $dllPath");
      if(dllPath == null) {
        log("[${hosting ? 'HOST' : 'GAME'}] The file doesn't exist");
        _onStop(
            reason: _StopReason.corruptedDllError,
            host: hosting
        );
        return;
      }

      log("[${hosting ? 'HOST' : 'GAME'}] Trying to inject ${injectable.name}...");
      await injectDll(gameProcess, dllPath.path);
      log("[${hosting ? 'HOST' : 'GAME'}] Injected ${injectable.name}");
    } catch (error, stackTrace) {
      log("[${hosting ? 'HOST' : 'GAME'}] Cannot inject ${injectable.name}: $error $stackTrace");
      _onStop(
          reason: _StopReason.corruptedDllError,
          host: hosting,
          error: error.toString(),
          stackTrace: stackTrace
      );
    }
  }

  String _getDllPath(_Injectable injectable) {
    switch(injectable){
      case _Injectable.reboot:
        return _settingsController.gameServerDll.text;
      case _Injectable.console:
        return _settingsController.unrealEngineConsoleDll.text;
      case _Injectable.sslBypassV2:
        return _settingsController.backendDll.text;
      case _Injectable.memoryFix:
        return _settingsController.memoryLeakDll.text;
    }
  }

  Future<File?> _getDllFileOrStop(_Injectable injectable, bool host) async {
    log("[${host ? 'HOST' : 'GAME'}] Checking dll ${injectable}...");
    final path = _getDllPath(injectable);
    log("[${host ? 'HOST' : 'GAME'}] Path: $path");
    final file = File(path);
    if(await file.exists()) {
      log("[${host ? 'HOST' : 'GAME'}] Path exists");
      return file;
    }

    log("[${host ? 'HOST' : 'GAME'}] Path does not exist, downloading critical dll again...");
    await downloadCriticalDllInteractive(path);
    log("[${host ? 'HOST' : 'GAME'}] Downloaded dll again, retrying check...");
    return _getDllFileOrStop(injectable, host);
  }

  InfoBarEntry _showLaunchingGameServerWidget() => _gameServerInfoBar = showInfoBar(
      translations.launchingHeadlessServer,
      loading: true,
      duration: null
  );

  InfoBarEntry _showLaunchingGameClientWidget() => _gameClientInfoBar = showInfoBar(
      translations.launchingGameClient,
      loading: true,
      duration: null
  );
}

enum _StopReason {
  normal,
  missingVersionError,
  missingExecutableError,
  corruptedVersionError,
  corruptedDllError,
  backendError,
  matchmakerError,
  tokenError,
  unknownError,
  exitCode;

  bool get isError => name.contains("Error");
}

enum _Injectable {
  console,
  sslBypassV2,
  reboot,
  memoryFix,
}
