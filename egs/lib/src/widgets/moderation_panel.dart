import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';

/// Modèle représentant un joueur connecté
class ConnectedPlayer {
  final String id;
  final String name;
  final String ip;
  final DateTime connectedAt;
  bool isBanned;

  ConnectedPlayer({
    required this.id,
    required this.name,
    required this.ip,
    required this.connectedAt,
    this.isBanned = false,
  });
}

/// Contrôleur pour la modération
class ModerationController extends GetxController {
  // Liste des joueurs connectés (simulé pour l'instant)
  final RxList<ConnectedPlayer> connectedPlayers = <ConnectedPlayer>[].obs;

  // Liste des joueurs bannis
  final RxList<ConnectedPlayer> bannedPlayers = <ConnectedPlayer>[].obs;

  /// Kick un joueur
  void kickPlayer(ConnectedPlayer player) {
    connectedPlayers.remove(player);
    // TODO: Implémenter la logique réelle de kick
  }

  /// Ban un joueur
  void banPlayer(ConnectedPlayer player) {
    player.isBanned = true;
    connectedPlayers.remove(player);
    bannedPlayers.add(player);
    // TODO: Implémenter la logique réelle de ban
  }

  /// Unban un joueur
  void unbanPlayer(ConnectedPlayer player) {
    player.isBanned = false;
    bannedPlayers.remove(player);
    // TODO: Implémenter la logique réelle de unban
  }

  /// Envoie un message à tous les joueurs
  void broadcastMessage(String message) {
    // TODO: Implémenter la logique d'envoi de message
  }
}

/// Panneau de modération des joueurs
class ModerationPanel extends StatefulWidget {
  const ModerationPanel({super.key});

  @override
  State<ModerationPanel> createState() => _ModerationPanelState();
}

class _ModerationPanelState extends State<ModerationPanel> {
  late ModerationController _moderationController;

  @override
  void initState() {
    super.initState();
    // Initialise le contrôleur de modération s'il n'existe pas
    if (!Get.isRegistered<ModerationController>()) {
      Get.put(ModerationController());
    }
    _moderationController = Get.find<ModerationController>();
  }

  @override
  Widget build(BuildContext context) {
    return ScaffoldPage.scrollable(
      header: PageHeader(
        title: const Text('Modération'),
        commandBar: CommandBar(
          primaryItems: [
            CommandBarButton(
              icon: const Icon(FluentIcons.message),
              label: const Text('Message global'),
              onPressed: _showBroadcastDialog,
            ),
          ],
        ),
      ),
      children: [
        // Joueurs connectés
        Card(
          child: Padding(
            padding: const EdgeInsets.all(16.0),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    const Icon(FluentIcons.people, size: 20),
                    const SizedBox(width: 12),
                    const Text(
                      'Joueurs connectés',
                      style: TextStyle(
                        fontSize: 18,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                    const Spacer(),
                    Obx(() => Text(
                          '${_moderationController.connectedPlayers.length} joueurs',
                          style: TextStyle(
                            fontSize: 12,
                            color: Colors.grey[100],
                          ),
                        )),
                  ],
                ),
                const SizedBox(height: 16),
                Obx(() {
                  if (_moderationController.connectedPlayers.isEmpty) {
                    return const Center(
                      child: Padding(
                        padding: EdgeInsets.all(32.0),
                        child: Text('Aucun joueur connecté'),
                      ),
                    );
                  }

                  return ListView.builder(
                    shrinkWrap: true,
                    physics: const NeverScrollableScrollPhysics(),
                    itemCount: _moderationController.connectedPlayers.length,
                    itemBuilder: (context, index) {
                      final player = _moderationController.connectedPlayers[index];
                      return _PlayerListItem(
                        player: player,
                        onKick: () => _confirmKick(player),
                        onBan: () => _confirmBan(player),
                      );
                    },
                  );
                }),
              ],
            ),
          ),
        ),

        const SizedBox(height: 24),

        // Joueurs bannis
        Card(
          child: Padding(
            padding: const EdgeInsets.all(16.0),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    const Icon(FluentIcons.blocked, size: 20),
                    const SizedBox(width: 12),
                    const Text(
                      'Joueurs bannis',
                      style: TextStyle(
                        fontSize: 18,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                    const Spacer(),
                    Obx(() => Text(
                          '${_moderationController.bannedPlayers.length} joueurs',
                          style: TextStyle(
                            fontSize: 12,
                            color: Colors.grey[100],
                          ),
                        )),
                  ],
                ),
                const SizedBox(height: 16),
                Obx(() {
                  if (_moderationController.bannedPlayers.isEmpty) {
                    return const Center(
                      child: Padding(
                        padding: EdgeInsets.all(32.0),
                        child: Text('Aucun joueur banni'),
                      ),
                    );
                  }

                  return ListView.builder(
                    shrinkWrap: true,
                    physics: const NeverScrollableScrollPhysics(),
                    itemCount: _moderationController.bannedPlayers.length,
                    itemBuilder: (context, index) {
                      final player = _moderationController.bannedPlayers[index];
                      return _BannedPlayerListItem(
                        player: player,
                        onUnban: () => _confirmUnban(player),
                      );
                    },
                  );
                }),
              ],
            ),
          ),
        ),

        const SizedBox(height: 24),

        // Informations
        Card(
          child: Padding(
            padding: const EdgeInsets.all(16.0),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Row(
                  children: [
                    Icon(FluentIcons.info, size: 20),
                    SizedBox(width: 12),
                    Text(
                      'Informations',
                      style: TextStyle(
                        fontSize: 18,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 16),
                Text(
                  'Note: La détection automatique des joueurs connectés nécessite une intégration avec le serveur de jeu. '
                  'Cette fonctionnalité affichera les joueurs une fois le serveur correctement configuré.',
                  style: TextStyle(
                    fontSize: 12,
                    color: Colors.grey[100],
                  ),
                ),
              ],
            ),
          ),
        ),
      ],
    );
  }

  void _confirmKick(ConnectedPlayer player) async {
    final result = await showDialog<bool>(
      context: context,
      builder: (context) => ContentDialog(
        title: const Text('Confirmer le kick'),
        content: Text('Voulez-vous vraiment expulser ${player.name} ?'),
        actions: [
          Button(
            onPressed: () => Navigator.of(context).pop(false),
            child: const Text('Annuler'),
          ),
          FilledButton(
            onPressed: () => Navigator.of(context).pop(true),
            child: const Text('Expulser'),
          ),
        ],
      ),
    );

    if (result == true) {
      _moderationController.kickPlayer(player);
      if (mounted) {
        displayInfoBar(
          context,
          builder: (context, close) => InfoBar(
            title: Text('${player.name} a été expulsé'),
            severity: InfoBarSeverity.success,
          ),
        );
      }
    }
  }

  void _confirmBan(ConnectedPlayer player) async {
    final result = await showDialog<bool>(
      context: context,
      builder: (context) => ContentDialog(
        title: const Text('Confirmer le bannissement'),
        content: Text('Voulez-vous vraiment bannir ${player.name} ?'),
        actions: [
          Button(
            onPressed: () => Navigator.of(context).pop(false),
            child: const Text('Annuler'),
          ),
          FilledButton(
            onPressed: () => Navigator.of(context).pop(true),
            child: const Text('Bannir'),
          ),
        ],
      ),
    );

    if (result == true) {
      _moderationController.banPlayer(player);
      if (mounted) {
        displayInfoBar(
          context,
          builder: (context, close) => InfoBar(
            title: Text('${player.name} a été banni'),
            severity: InfoBarSeverity.success,
          ),
        );
      }
    }
  }

  void _confirmUnban(ConnectedPlayer player) async {
    final result = await showDialog<bool>(
      context: context,
      builder: (context) => ContentDialog(
        title: const Text('Confirmer le débannissement'),
        content: Text('Voulez-vous vraiment débannir ${player.name} ?'),
        actions: [
          Button(
            onPressed: () => Navigator.of(context).pop(false),
            child: const Text('Annuler'),
          ),
          FilledButton(
            onPressed: () => Navigator.of(context).pop(true),
            child: const Text('Débannir'),
          ),
        ],
      ),
    );

    if (result == true) {
      _moderationController.unbanPlayer(player);
      if (mounted) {
        displayInfoBar(
          context,
          builder: (context, close) => InfoBar(
            title: Text('${player.name} a été débanni'),
            severity: InfoBarSeverity.success,
          ),
        );
      }
    }
  }

  void _showBroadcastDialog() async {
    final messageController = TextEditingController();

    final message = await showDialog<String>(
      context: context,
      builder: (context) => ContentDialog(
        title: const Text('Message global'),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text('Envoyer un message à tous les joueurs:'),
            const SizedBox(height: 16),
            TextBox(
              controller: messageController,
              placeholder: 'Votre message...',
              maxLines: 3,
            ),
          ],
        ),
        actions: [
          Button(
            onPressed: () => Navigator.of(context).pop(),
            child: const Text('Annuler'),
          ),
          FilledButton(
            onPressed: () => Navigator.of(context).pop(messageController.text),
            child: const Text('Envoyer'),
          ),
        ],
      ),
    );

    if (message != null && message.isNotEmpty) {
      _moderationController.broadcastMessage(message);
      if (mounted) {
        displayInfoBar(
          context,
          builder: (context, close) => const InfoBar(
            title: Text('Message envoyé'),
            severity: InfoBarSeverity.success,
          ),
        );
      }
    }
  }
}

class _PlayerListItem extends StatelessWidget {
  final ConnectedPlayer player;
  final VoidCallback onKick;
  final VoidCallback onBan;

  const _PlayerListItem({
    required this.player,
    required this.onKick,
    required this.onBan,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      margin: const EdgeInsets.only(bottom: 8),
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        border: Border.all(color: Colors.grey[60]),
        borderRadius: BorderRadius.circular(4),
      ),
      child: Row(
        children: [
          const Icon(FluentIcons.contact, size: 24),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  player.name,
                  style: const TextStyle(
                    fontWeight: FontWeight.w600,
                    fontSize: 14,
                  ),
                ),
                const SizedBox(height: 4),
                Text(
                  'IP: ${player.ip}',
                  style: TextStyle(
                    fontSize: 12,
                    color: Colors.grey[100],
                  ),
                ),
                Text(
                  'Connecté depuis: ${_formatDuration(DateTime.now().difference(player.connectedAt))}',
                  style: TextStyle(
                    fontSize: 11,
                    color: Colors.grey[80],
                  ),
                ),
              ],
            ),
          ),
          Button(
            onPressed: onKick,
            child: const Text('Kick'),
          ),
          const SizedBox(width: 8),
          Button(
            onPressed: onBan,
            child: const Text('Ban'),
          ),
        ],
      ),
    );
  }

  String _formatDuration(Duration duration) {
    if (duration.inHours > 0) {
      return '${duration.inHours}h ${duration.inMinutes.remainder(60)}m';
    } else if (duration.inMinutes > 0) {
      return '${duration.inMinutes}m ${duration.inSeconds.remainder(60)}s';
    } else {
      return '${duration.inSeconds}s';
    }
  }
}

class _BannedPlayerListItem extends StatelessWidget {
  final ConnectedPlayer player;
  final VoidCallback onUnban;

  const _BannedPlayerListItem({
    required this.player,
    required this.onUnban,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      margin: const EdgeInsets.only(bottom: 8),
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        border: Border.all(color: Colors.grey[60]),
        borderRadius: BorderRadius.circular(4),
      ),
      child: Row(
        children: [
          const Icon(FluentIcons.blocked, size: 24, color: Colors.red),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  player.name,
                  style: const TextStyle(
                    fontWeight: FontWeight.w600,
                    fontSize: 14,
                  ),
                ),
                const SizedBox(height: 4),
                Text(
                  'IP: ${player.ip}',
                  style: TextStyle(
                    fontSize: 12,
                    color: Colors.grey[100],
                  ),
                ),
              ],
            ),
          ),
          FilledButton(
            onPressed: onUnban,
            child: const Text('Débannir'),
          ),
        ],
      ),
    );
  }
}
