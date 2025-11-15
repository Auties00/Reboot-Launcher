import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import '../controllers/server_controller.dart';

/// Panneau d'affichage des logs du serveur
class LogsPanel extends StatelessWidget {
  const LogsPanel({super.key});

  @override
  Widget build(BuildContext context) {
    final ServerController controller = Get.find();

    return ScaffoldPage(
      header: PageHeader(
        title: const Text('Logs du serveur'),
        commandBar: CommandBar(
          primaryItems: [
            CommandBarButton(
              icon: const Icon(FluentIcons.clear),
              label: const Text('Effacer'),
              onPressed: controller.clearLogs,
            ),
            CommandBarButton(
              icon: const Icon(FluentIcons.refresh),
              label: const Text('Actualiser'),
              onPressed: () {
                // Force une mise à jour
                controller.serverLogs.refresh();
              },
            ),
          ],
        ),
      ),
      content: Padding(
        padding: const EdgeInsets.all(16.0),
        child: Card(
          child: Padding(
            padding: const EdgeInsets.all(16.0),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    const Icon(FluentIcons.page_list, size: 20),
                    const SizedBox(width: 12),
                    const Text(
                      'Console du serveur',
                      style: TextStyle(
                        fontSize: 18,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                    const Spacer(),
                    Obx(() => Text(
                          '${controller.serverLogs.length} lignes',
                          style: TextStyle(
                            fontSize: 12,
                            color: Colors.grey[100],
                          ),
                        )),
                  ],
                ),
                const SizedBox(height: 16),
                Expanded(
                  child: Obx(() {
                    if (controller.serverLogs.isEmpty) {
                      return Center(
                        child: Column(
                          mainAxisAlignment: MainAxisAlignment.center,
                          children: [
                            Icon(
                              FluentIcons.page_list,
                              size: 48,
                              color: Colors.grey[80],
                            ),
                            const SizedBox(height: 16),
                            Text(
                              'Aucun log disponible',
                              style: TextStyle(
                                fontSize: 14,
                                color: Colors.grey[100],
                              ),
                            ),
                            const SizedBox(height: 8),
                            Text(
                              'Démarrez le serveur pour voir les logs',
                              style: TextStyle(
                                fontSize: 12,
                                color: Colors.grey[80],
                              ),
                            ),
                          ],
                        ),
                      );
                    }

                    return Container(
                      decoration: BoxDecoration(
                        color: Colors.black,
                        borderRadius: BorderRadius.circular(4),
                        border: Border.all(color: Colors.grey[60]),
                      ),
                      child: ListView.builder(
                        padding: const EdgeInsets.all(8),
                        itemCount: controller.serverLogs.length,
                        itemBuilder: (context, index) {
                          final log = controller.serverLogs[index];
                          return _LogEntry(log: log);
                        },
                      ),
                    );
                  }),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

class _LogEntry extends StatelessWidget {
  final String log;

  const _LogEntry({required this.log});

  @override
  Widget build(BuildContext context) {
    Color textColor = Colors.white;
    FontWeight fontWeight = FontWeight.normal;

    // Coloration selon le type de log
    if (log.contains('[ERREUR]')) {
      textColor = Colors.red;
      fontWeight = FontWeight.w600;
    } else if (log.contains('[STDERR]')) {
      textColor = Colors.orange;
    } else if (log.contains('[STDOUT]')) {
      textColor = const Color(0xFF00FF00); // Vert console
    } else if (log.contains('Démarrage') ||
        log.contains('Processus démarré') ||
        log.contains('DLL injectée')) {
      textColor = Colors.blue;
    } else if (log.contains('arrêté') || log.contains('Arrêt')) {
      textColor = Colors.yellow;
    }

    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 2),
      child: SelectableText(
        log,
        style: TextStyle(
          fontFamily: 'Consolas, Monaco, monospace',
          fontSize: 12,
          color: textColor,
          fontWeight: fontWeight,
        ),
      ),
    );
  }
}
