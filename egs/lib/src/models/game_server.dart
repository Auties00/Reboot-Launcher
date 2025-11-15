import 'package:get_storage/get_storage.dart';

/// Représente un serveur de jeu Fortnite
class GameServer {
  /// Identifiant unique du serveur
  final String id;

  /// Nom du serveur
  String name;

  /// Mot de passe du serveur (peut être vide)
  String password;

  /// Version de Fortnite (ex: "Season 3", "Season 20", etc.)
  String version;

  /// Playlist du serveur (ex: "Solo", "Duo", "Squad", "Arena")
  String playlist;

  /// Chemin vers l'exécutable du jeu
  String gamePath;

  /// Port du serveur
  int port;

  /// Nombre maximum de joueurs
  int maxPlayers;

  /// DLLs sélectionnées pour ce serveur
  Map<String, String> selectedDlls;

  /// Indicateur si le serveur est actuellement en cours d'exécution
  bool isRunning;

  /// PID du processus du serveur (si en cours d'exécution)
  int? processPid;

  /// Date de création du serveur
  DateTime createdAt;

  /// Date de dernière modification
  DateTime updatedAt;

  GameServer({
    required this.id,
    required this.name,
    this.password = '',
    required this.version,
    required this.playlist,
    required this.gamePath,
    this.port = 7777,
    this.maxPlayers = 100,
    Map<String, String>? selectedDlls,
    this.isRunning = false,
    this.processPid,
    DateTime? createdAt,
    DateTime? updatedAt,
  })  : selectedDlls = selectedDlls ?? {},
        createdAt = createdAt ?? DateTime.now(),
        updatedAt = updatedAt ?? DateTime.now();

  /// Convertit le serveur en Map pour la sauvegarde
  Map<String, dynamic> toJson() {
    return {
      'id': id,
      'name': name,
      'password': password,
      'version': version,
      'playlist': playlist,
      'gamePath': gamePath,
      'port': port,
      'maxPlayers': maxPlayers,
      'selectedDlls': selectedDlls,
      'isRunning': isRunning,
      'processPid': processPid,
      'createdAt': createdAt.toIso8601String(),
      'updatedAt': updatedAt.toIso8601String(),
    };
  }

  /// Crée un serveur depuis une Map
  factory GameServer.fromJson(Map<String, dynamic> json) {
    return GameServer(
      id: json['id'] as String,
      name: json['name'] as String,
      password: json['password'] as String? ?? '',
      version: json['version'] as String,
      playlist: json['playlist'] as String,
      gamePath: json['gamePath'] as String,
      port: json['port'] as int? ?? 7777,
      maxPlayers: json['maxPlayers'] as int? ?? 100,
      selectedDlls: Map<String, String>.from(json['selectedDlls'] as Map? ?? {}),
      isRunning: json['isRunning'] as bool? ?? false,
      processPid: json['processPid'] as int?,
      createdAt: DateTime.parse(json['createdAt'] as String),
      updatedAt: DateTime.parse(json['updatedAt'] as String),
    );
  }

  /// Crée une copie du serveur avec des modifications
  GameServer copyWith({
    String? name,
    String? password,
    String? version,
    String? playlist,
    String? gamePath,
    int? port,
    int? maxPlayers,
    Map<String, String>? selectedDlls,
    bool? isRunning,
    int? processPid,
    DateTime? updatedAt,
  }) {
    return GameServer(
      id: id,
      name: name ?? this.name,
      password: password ?? this.password,
      version: version ?? this.version,
      playlist: playlist ?? this.playlist,
      gamePath: gamePath ?? this.gamePath,
      port: port ?? this.port,
      maxPlayers: maxPlayers ?? this.maxPlayers,
      selectedDlls: selectedDlls ?? this.selectedDlls,
      isRunning: isRunning ?? this.isRunning,
      processPid: processPid ?? this.processPid,
      createdAt: createdAt,
      updatedAt: updatedAt ?? DateTime.now(),
    );
  }
}

/// Service de gestion des serveurs avec stockage persistant
class ServerStorage {
  static const String _storageKey = 'egs_servers';
  final GetStorage _storage = GetStorage('egs_storage');

  /// Récupère tous les serveurs sauvegardés
  List<GameServer> getServers() {
    final List<dynamic>? serversJson = _storage.read<List<dynamic>>(_storageKey);
    if (serversJson == null) return [];

    return serversJson
        .map((json) => GameServer.fromJson(json as Map<String, dynamic>))
        .toList();
  }

  /// Sauvegarde un serveur
  Future<void> saveServer(GameServer server) async {
    final servers = getServers();
    final index = servers.indexWhere((s) => s.id == server.id);

    if (index >= 0) {
      servers[index] = server;
    } else {
      servers.add(server);
    }

    await _storage.write(_storageKey, servers.map((s) => s.toJson()).toList());
  }

  /// Supprime un serveur
  Future<void> deleteServer(String serverId) async {
    final servers = getServers();
    servers.removeWhere((s) => s.id == serverId);
    await _storage.write(_storageKey, servers.map((s) => s.toJson()).toList());
  }

  /// Récupère un serveur par son ID
  GameServer? getServerById(String id) {
    final servers = getServers();
    try {
      return servers.firstWhere((s) => s.id == id);
    } catch (_) {
      return null;
    }
  }
}
