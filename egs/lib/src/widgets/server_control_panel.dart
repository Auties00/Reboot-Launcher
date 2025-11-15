import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import '../controllers/server_controller.dart';

/// Panneau de contrôle du serveur (démarrer, arrêter, redémarrer)
class ServerControlPanel extends StatelessWidget {
  const ServerControlPanel({super.key});

  @override
  Widget build(BuildContext context) {
    final ServerController controller = Get.find();

    return ScaffoldPage.scrollable(
      header: const PageHeader(
        title: Text('Contrôle du serveur'),
      ),
      children: [
        Obx(() {
          final server = controller.selectedServer.value;
          if (server == null) return const SizedBox();

          return Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              // Informations du serveur
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(16.0),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      const Text(
                        'Informations du serveur',
                        style: TextStyle(
                          fontSize: 18,
                          fontWeight: FontWeight.bold,
                        ),
                      ),
                      const SizedBox(height: 16),
                      _InfoRow(label: 'Nom', value: server.name),
                      const SizedBox(height: 8),
                      _InfoRow(label: 'Version', value: server.version),
                      const SizedBox(height: 8),
                      _InfoRow(label: 'Playlist', value: server.playlist),
                      const SizedBox(height: 8),
                      _InfoRow(label: 'Port', value: server.port.toString()),
                      const SizedBox(height: 8),
                      _InfoRow(
                        label: 'Joueurs max',
                        value: server.maxPlayers.toString(),
                      ),
                      const SizedBox(height: 8),
                      _InfoRow(
                        label: 'Mot de passe',
                        value: server.password.isEmpty ? 'Non' : 'Oui',
                      ),
                      const SizedBox(height: 8),
                      _InfoRow(
                        label: 'PID',
                        value: server.processPid?.toString() ?? 'N/A',
                      ),
                    ],
                  ),
                ),
              ),

              const SizedBox(height: 24),

              // Contrôles
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(16.0),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      const Text(
                        'Contrôles',
                        style: TextStyle(
                          fontSize: 18,
                          fontWeight: FontWeight.bold,
                        ),
                      ),
                      const SizedBox(height: 16),
                      Wrap(
                        spacing: 12,
                        runSpacing: 12,
                        children: [
                          // Bouton Démarrer
                          FilledButton(
                            onPressed: server.isRunning
                                ? null
                                : () => _startServer(context, controller, server),
                            child: Row(
                              mainAxisSize: MainAxisSize.min,
                              children: const [
                                Icon(FluentIcons.play_solid, size: 16),
                                SizedBox(width: 8),
                                Text('Démarrer'),
                              ],
                            ),
                          ),

                          // Bouton Arrêter
                          Button(
                            onPressed: !server.isRunning
                                ? null
                                : () => _stopServer(context, controller, server),
                            child: Row(
                              mainAxisSize: MainAxisSize.min,
                              children: const [
                                Icon(FluentIcons.stop_solid, size: 16),
                                SizedBox(width: 8),
                                Text('Arrêter'),
                              ],
                            ),
                          ),

                          // Bouton Redémarrer
                          Button(
                            onPressed: !server.isRunning
                                ? null
                                : () => _restartServer(context, controller, server),
                            child: Row(
                              mainAxisSize: MainAxisSize.min,
                              children: const [
                                Icon(FluentIcons.refresh, size: 16),
                                SizedBox(width: 8),
                                Text('Redémarrer'),
                              ],
                            ),
                          ),
                        ],
                      ),
                    ],
                  ),
                ),
              ),

              const SizedBox(height: 24),

              // Configuration
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(16.0),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      const Text(
                        'Configuration',
                        style: TextStyle(
                          fontSize: 18,
                          fontWeight: FontWeight.bold,
                        ),
                      ),
                      const SizedBox(height: 16),
                      Row(
                        children: [
                          Expanded(
                            child: InfoLabel(
                              label: 'Chemin du jeu',
                              child: TextBox(
                                controller: TextEditingController(
                                  text: server.gamePath,
                                ),
                                readOnly: true,
                              ),
                            ),
                          ),
                        ],
                      ),
                    ],
                  ),
                ),
              ),
            ],
          );
        }),
      ],
    );
  }

  Future<void> _startServer(
    BuildContext context,
    ServerController controller,
    server,
  ) async {
    final success = await controller.startServer(server);
    if (context.mounted) {
      displayInfoBar(
        context,
        builder: (context, close) => InfoBar(
          title: Text(
            success ? 'Serveur démarré' : 'Échec du démarrage',
          ),
          severity: success ? InfoBarSeverity.success : InfoBarSeverity.error,
        ),
      );
    }
  }

  Future<void> _stopServer(
    BuildContext context,
    ServerController controller,
    server,
  ) async {
    final success = await controller.stopServer(server);
    if (context.mounted) {
      displayInfoBar(
        context,
        builder: (context, close) => InfoBar(
          title: Text(
            success ? 'Serveur arrêté' : 'Échec de l\'arrêt',
          ),
          severity: success ? InfoBarSeverity.success : InfoBarSeverity.error,
        ),
      );
    }
  }

  Future<void> _restartServer(
    BuildContext context,
    ServerController controller,
    server,
  ) async {
    final success = await controller.restartServer(server);
    if (context.mounted) {
      displayInfoBar(
        context,
        builder: (context, close) => InfoBar(
          title: Text(
            success ? 'Serveur redémarré' : 'Échec du redémarrage',
          ),
          severity: success ? InfoBarSeverity.success : InfoBarSeverity.error,
        ),
      );
    }
  }
}

class _InfoRow extends StatelessWidget {
  final String label;
  final String value;

  const _InfoRow({
    required this.label,
    required this.value,
  });

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        SizedBox(
          width: 120,
          child: Text(
            '$label:',
            style: const TextStyle(fontWeight: FontWeight.w600),
          ),
        ),
        Text(value),
      ],
    );
  }
}
