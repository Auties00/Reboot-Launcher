import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import '../controllers/server_controller.dart';

/// Contrôleur pour les statistiques du serveur
class StatisticsController extends GetxController {
  // Statistiques du serveur
  final Rx<DateTime?> serverStartTime = Rx<DateTime?>(null);
  final RxInt totalPlayersConnected = 0.obs;
  final RxInt currentPlayers = 0.obs;
  final RxInt peakPlayers = 0.obs;
  final RxDouble cpuUsage = 0.0.obs;
  final RxDouble memoryUsage = 0.0.obs;
  final RxInt networkIn = 0.obs;
  final RxInt networkOut = 0.obs;

  /// Calcule le temps de fonctionnement
  String get uptime {
    if (serverStartTime.value == null) {
      return 'Serveur arrêté';
    }

    final duration = DateTime.now().difference(serverStartTime.value!);
    final hours = duration.inHours;
    final minutes = duration.inMinutes.remainder(60);
    final seconds = duration.inSeconds.remainder(60);

    return '${hours}h ${minutes}m ${seconds}s';
  }

  /// Démarre le suivi des statistiques
  void startTracking() {
    serverStartTime.value = DateTime.now();
  }

  /// Arrête le suivi des statistiques
  void stopTracking() {
    serverStartTime.value = null;
    currentPlayers.value = 0;
  }

  /// Met à jour le nombre de joueurs actuels
  void updateCurrentPlayers(int count) {
    currentPlayers.value = count;
    totalPlayersConnected.value += count;

    if (count > peakPlayers.value) {
      peakPlayers.value = count;
    }
  }
}

/// Panneau d'affichage des statistiques et données du serveur
class StatisticsPanel extends StatefulWidget {
  const StatisticsPanel({super.key});

  @override
  State<StatisticsPanel> createState() => _StatisticsPanelState();
}

class _StatisticsPanelState extends State<StatisticsPanel> {
  late StatisticsController _statsController;

  @override
  void initState() {
    super.initState();
    // Initialise le contrôleur de statistiques s'il n'existe pas
    if (!Get.isRegistered<StatisticsController>()) {
      Get.put(StatisticsController());
    }
    _statsController = Get.find<StatisticsController>();
  }

  @override
  Widget build(BuildContext context) {
    final ServerController serverController = Get.find();

    return ScaffoldPage.scrollable(
      header: const PageHeader(
        title: Text('Statistiques du serveur'),
      ),
      children: [
        // Temps de fonctionnement
        Card(
          child: Padding(
            padding: const EdgeInsets.all(16.0),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Row(
                  children: [
                    Icon(FluentIcons.clock, size: 20),
                    SizedBox(width: 12),
                    Text(
                      'Temps de fonctionnement',
                      style: TextStyle(
                        fontSize: 18,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 16),
                Obx(() => Text(
                      _statsController.uptime,
                      style: const TextStyle(
                        fontSize: 24,
                        fontWeight: FontWeight.w600,
                      ),
                    )),
              ],
            ),
          ),
        ),

        const SizedBox(height: 16),

        // Statistiques des joueurs
        Card(
          child: Padding(
            padding: const EdgeInsets.all(16.0),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Row(
                  children: [
                    Icon(FluentIcons.people, size: 20),
                    SizedBox(width: 12),
                    Text(
                      'Joueurs',
                      style: TextStyle(
                        fontSize: 18,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 16),
                Row(
                  children: [
                    Expanded(
                      child: Obx(() => _StatCard(
                            icon: FluentIcons.contact,
                            label: 'Actuels',
                            value: _statsController.currentPlayers.value.toString(),
                            color: Colors.blue,
                          )),
                    ),
                    const SizedBox(width: 16),
                    Expanded(
                      child: Obx(() => _StatCard(
                            icon: FluentIcons.trending_up,
                            label: 'Record',
                            value: _statsController.peakPlayers.value.toString(),
                            color: Colors.green,
                          )),
                    ),
                  ],
                ),
                const SizedBox(height: 12),
                Obx(() => _StatCard(
                      icon: FluentIcons.group,
                      label: 'Total connectés',
                      value: _statsController.totalPlayersConnected.value.toString(),
                      color: Colors.purple,
                    )),
              ],
            ),
          ),
        ),

        const SizedBox(height: 16),

        // Utilisation des ressources
        Card(
          child: Padding(
            padding: const EdgeInsets.all(16.0),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Row(
                  children: [
                    Icon(FluentIcons.chart, size: 20),
                    SizedBox(width: 12),
                    Text(
                      'Ressources système',
                      style: TextStyle(
                        fontSize: 18,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 16),
                Obx(() => _ProgressStat(
                      icon: FluentIcons.processing,
                      label: 'CPU',
                      value: _statsController.cpuUsage.value,
                      color: Colors.orange,
                    )),
                const SizedBox(height: 12),
                Obx(() => _ProgressStat(
                      icon: FluentIcons.server,
                      label: 'Mémoire',
                      value: _statsController.memoryUsage.value,
                      color: Colors.teal,
                    )),
              ],
            ),
          ),
        ),

        const SizedBox(height: 16),

        // Réseau
        Card(
          child: Padding(
            padding: const EdgeInsets.all(16.0),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Row(
                  children: [
                    Icon(FluentIcons.network_tower, size: 20),
                    SizedBox(width: 12),
                    Text(
                      'Réseau',
                      style: TextStyle(
                        fontSize: 18,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 16),
                Row(
                  children: [
                    Expanded(
                      child: Obx(() => _StatCard(
                            icon: FluentIcons.download,
                            label: 'Entrant',
                            value: _formatBytes(_statsController.networkIn.value),
                            color: Colors.blue,
                          )),
                    ),
                    const SizedBox(width: 16),
                    Expanded(
                      child: Obx(() => _StatCard(
                            icon: FluentIcons.upload,
                            label: 'Sortant',
                            value: _formatBytes(_statsController.networkOut.value),
                            color: Colors.green,
                          )),
                    ),
                  ],
                ),
              ],
            ),
          ),
        ),

        const SizedBox(height: 16),

        // Informations serveur
        Obx(() {
          final server = serverController.selectedServer.value;
          if (server == null) return const SizedBox();

          return Card(
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
                        'Informations détaillées',
                        style: TextStyle(
                          fontSize: 18,
                          fontWeight: FontWeight.bold,
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 16),
                  _DetailRow(label: 'Nom du serveur', value: server.name),
                  const Divider(),
                  _DetailRow(label: 'Version', value: server.version),
                  const Divider(),
                  _DetailRow(label: 'Playlist', value: server.playlist),
                  const Divider(),
                  _DetailRow(label: 'Port', value: server.port.toString()),
                  const Divider(),
                  _DetailRow(
                    label: 'Capacité max',
                    value: '${server.maxPlayers} joueurs',
                  ),
                  const Divider(),
                  _DetailRow(
                    label: 'Mot de passe',
                    value: server.password.isEmpty ? 'Non défini' : 'Protégé',
                  ),
                  const Divider(),
                  _DetailRow(
                    label: 'DLLs chargées',
                    value: '${server.selectedDlls.length}',
                  ),
                  const Divider(),
                  _DetailRow(
                    label: 'Créé le',
                    value: _formatDate(server.createdAt),
                  ),
                  const Divider(),
                  _DetailRow(
                    label: 'Modifié le',
                    value: _formatDate(server.updatedAt),
                  ),
                ],
              ),
            ),
          );
        }),
      ],
    );
  }

  String _formatBytes(int bytes) {
    if (bytes < 1024) {
      return '$bytes B';
    } else if (bytes < 1024 * 1024) {
      return '${(bytes / 1024).toStringAsFixed(1)} KB';
    } else if (bytes < 1024 * 1024 * 1024) {
      return '${(bytes / (1024 * 1024)).toStringAsFixed(1)} MB';
    } else {
      return '${(bytes / (1024 * 1024 * 1024)).toStringAsFixed(1)} GB';
    }
  }

  String _formatDate(DateTime date) {
    return '${date.day.toString().padLeft(2, '0')}/${date.month.toString().padLeft(2, '0')}/${date.year} '
        '${date.hour.toString().padLeft(2, '0')}:${date.minute.toString().padLeft(2, '0')}';
  }
}

class _StatCard extends StatelessWidget {
  final IconData icon;
  final String label;
  final String value;
  final Color color;

  const _StatCard({
    required this.icon,
    required this.label,
    required this.value,
    required this.color,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        border: Border.all(color: Colors.grey[60]),
        borderRadius: BorderRadius.circular(4),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(icon, size: 16, color: color),
              const SizedBox(width: 8),
              Text(
                label,
                style: TextStyle(
                  fontSize: 12,
                  color: Colors.grey[100],
                ),
              ),
            ],
          ),
          const SizedBox(height: 8),
          Text(
            value,
            style: TextStyle(
              fontSize: 24,
              fontWeight: FontWeight.bold,
              color: color,
            ),
          ),
        ],
      ),
    );
  }
}

class _ProgressStat extends StatelessWidget {
  final IconData icon;
  final String label;
  final double value;
  final Color color;

  const _ProgressStat({
    required this.icon,
    required this.label,
    required this.value,
    required this.color,
  });

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            Icon(icon, size: 16, color: color),
            const SizedBox(width: 8),
            Text(
              label,
              style: const TextStyle(
                fontSize: 14,
                fontWeight: FontWeight.w600,
              ),
            ),
            const Spacer(),
            Text(
              '${value.toStringAsFixed(1)}%',
              style: TextStyle(
                fontSize: 14,
                fontWeight: FontWeight.bold,
                color: color,
              ),
            ),
          ],
        ),
        const SizedBox(height: 8),
        ProgressBar(
          value: value,
          strokeWidth: 8,
        ),
      ],
    );
  }
}

class _DetailRow extends StatelessWidget {
  final String label;
  final String value;

  const _DetailRow({
    required this.label,
    required this.value,
  });

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 8.0),
      child: Row(
        children: [
          SizedBox(
            width: 150,
            child: Text(
              label,
              style: const TextStyle(
                fontWeight: FontWeight.w600,
                fontSize: 13,
              ),
            ),
          ),
          Expanded(
            child: Text(
              value,
              style: const TextStyle(fontSize: 13),
            ),
          ),
        ],
      ),
    );
  }
}
