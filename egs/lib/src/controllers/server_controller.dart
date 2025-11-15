import 'dart:async';
import 'dart:io';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import '../models/game_server.dart';

/// Contrôleur principal pour la gestion des serveurs
class ServerController extends GetxController {
  final ServerStorage _storage = ServerStorage();

  /// Liste de tous les serveurs
  final RxList<GameServer> servers = <GameServer>[].obs;

  /// Serveur actuellement sélectionné
  final Rxn<GameServer> selectedServer = Rxn<GameServer>();

  /// Logs du serveur en cours d'exécution
  final RxList<String> serverLogs = <String>[].obs;

  /// Stream pour écouter les logs du processus
  StreamSubscription? _logSubscription;

  @override
  void onInit() {
    super.onInit();
    loadServers();
  }

  @override
  void onClose() {
    _logSubscription?.cancel();
    super.onClose();
  }

  /// Charge tous les serveurs depuis le stockage
  void loadServers() {
    servers.value = _storage.getServers();
  }

  /// Crée un nouveau serveur
  Future<void> createServer({
    required String name,
    required String password,
    required String version,
    required String playlist,
    required String gamePath,
    int port = 7777,
    int maxPlayers = 100,
  }) async {
    final server = GameServer(
      id: DateTime.now().millisecondsSinceEpoch.toString(),
      name: name,
      password: password,
      version: version,
      playlist: playlist,
      gamePath: gamePath,
      port: port,
      maxPlayers: maxPlayers,
    );

    await _storage.saveServer(server);
    loadServers();
  }

  /// Sélectionne un serveur
  void selectServer(GameServer server) {
    selectedServer.value = server;
    serverLogs.clear();
  }

  /// Met à jour un serveur
  Future<void> updateServer(GameServer server) async {
    await _storage.saveServer(server);
    loadServers();

    // Met à jour la sélection si c'est le serveur actuel
    if (selectedServer.value?.id == server.id) {
      selectedServer.value = server;
    }
  }

  /// Supprime un serveur
  Future<void> deleteServer(String serverId) async {
    // Si le serveur est en cours d'exécution, l'arrêter d'abord
    final server = _storage.getServerById(serverId);
    if (server != null && server.isRunning) {
      await stopServer(server);
    }

    await _storage.deleteServer(serverId);
    loadServers();

    // Désélectionne si c'était le serveur actuel
    if (selectedServer.value?.id == serverId) {
      selectedServer.value = null;
      serverLogs.clear();
    }
  }

  /// Démarre un serveur
  Future<bool> startServer(GameServer server) async {
    try {
      serverLogs.add('[${DateTime.now().toString()}] Démarrage du serveur ${server.name}...');

      // Vérifier que le chemin du jeu existe
      final gameFile = File(server.gamePath);
      if (!await gameFile.exists()) {
        serverLogs.add('[ERREUR] Le fichier du jeu n\'existe pas: ${server.gamePath}');
        return false;
      }

      // Préparer les arguments de lancement
      final args = [
        '-log',
        '-port=${server.port}',
        '-maxplayers=${server.maxPlayers}',
      ];

      if (server.password.isNotEmpty) {
        args.add('-password=${server.password}');
      }

      serverLogs.add('[${DateTime.now().toString()}] Lancement avec les arguments: ${args.join(' ')}');

      // Démarrer le processus du serveur
      final process = await Process.start(
        server.gamePath,
        args,
        workingDirectory: gameFile.parent.path,
      );

      final pid = process.pid;
      serverLogs.add('[${DateTime.now().toString()}] Processus démarré (PID: $pid)');

      // Mettre à jour le serveur
      final updatedServer = server.copyWith(
        isRunning: true,
        processPid: pid,
      );
      await updateServer(updatedServer);

      // Écouter les logs du processus
      _logSubscription?.cancel();
      _logSubscription = process.stdout.transform(systemEncoding.decoder).listen((data) {
        for (var line in data.split('\n')) {
          if (line.trim().isNotEmpty) {
            serverLogs.add('[STDOUT] $line');
          }
        }
      });

      process.stderr.transform(systemEncoding.decoder).listen((data) {
        for (var line in data.split('\n')) {
          if (line.trim().isNotEmpty) {
            serverLogs.add('[STDERR] $line');
          }
        }
      });

      // Surveiller la fin du processus
      process.exitCode.then((exitCode) {
        serverLogs.add('[${DateTime.now().toString()}] Le serveur s\'est arrêté (code: $exitCode)');
        final stoppedServer = server.copyWith(
          isRunning: false,
          processPid: null,
        );
        updateServer(stoppedServer);
      });

      return true;
    } catch (e) {
      serverLogs.add('[ERREUR] Échec du démarrage: $e');
      return false;
    }
  }

  /// Arrête un serveur
  Future<bool> stopServer(GameServer server) async {
    try {
      if (!server.isRunning || server.processPid == null) {
        serverLogs.add('[${DateTime.now().toString()}] Le serveur n\'est pas en cours d\'exécution');
        return false;
      }

      serverLogs.add('[${DateTime.now().toString()}] Arrêt du serveur (PID: ${server.processPid})...');

      // Tuer le processus
      Process.killPid(server.processPid!);

      // Mettre à jour le serveur
      final updatedServer = server.copyWith(
        isRunning: false,
        processPid: null,
      );
      await updateServer(updatedServer);

      _logSubscription?.cancel();
      serverLogs.add('[${DateTime.now().toString()}] Serveur arrêté avec succès');

      return true;
    } catch (e) {
      serverLogs.add('[ERREUR] Échec de l\'arrêt: $e');
      return false;
    }
  }

  /// Redémarre un serveur
  Future<bool> restartServer(GameServer server) async {
    serverLogs.add('[${DateTime.now().toString()}] Redémarrage du serveur...');
    await stopServer(server);
    await Future.delayed(const Duration(seconds: 2));
    return await startServer(server);
  }

  /// Injecte une DLL dans le processus du serveur
  Future<bool> injectDll(GameServer server, String dllPath) async {
    try {
      if (!server.isRunning || server.processPid == null) {
        serverLogs.add('[ERREUR] Le serveur doit être en cours d\'exécution pour injecter une DLL');
        return false;
      }

      final dllFile = File(dllPath);
      if (!await dllFile.exists()) {
        serverLogs.add('[ERREUR] Le fichier DLL n\'existe pas: $dllPath');
        return false;
      }

      serverLogs.add('[${DateTime.now().toString()}] Injection de la DLL: ${dllFile.path}');
      await injectDll(server.processPid!, dllFile);
      serverLogs.add('[${DateTime.now().toString()}] DLL injectée avec succès');

      return true;
    } catch (e) {
      serverLogs.add('[ERREUR] Échec de l\'injection de DLL: $e');
      return false;
    }
  }

  /// Efface les logs
  void clearLogs() {
    serverLogs.clear();
  }
}
