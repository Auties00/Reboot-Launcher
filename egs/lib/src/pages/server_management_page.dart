import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import '../controllers/server_controller.dart';
import '../widgets/server_control_panel.dart';
import '../widgets/dll_selector_panel.dart';
import '../widgets/logs_panel.dart';
import '../widgets/moderation_panel.dart';
import '../widgets/statistics_panel.dart';

/// Page principale de gestion d'un serveur
class ServerManagementPage extends StatefulWidget {
  const ServerManagementPage({super.key});

  @override
  State<ServerManagementPage> createState() => _ServerManagementPageState();
}

class _ServerManagementPageState extends State<ServerManagementPage> {
  int _selectedIndex = 0;

  @override
  Widget build(BuildContext context) {
    final ServerController controller = Get.find();

    return Obx(() {
      final server = controller.selectedServer.value;

      if (server == null) {
        return ScaffoldPage(
          content: const Center(
            child: Text('Aucun serveur sélectionné'),
          ),
        );
      }

      return NavigationView(
        appBar: NavigationAppBar(
          title: Row(
            children: [
              const Icon(FluentIcons.server, size: 20),
              const SizedBox(width: 12),
              Text(server.name),
              const SizedBox(width: 16),
              Container(
                padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
                decoration: BoxDecoration(
                  color: server.isRunning ? Colors.green : Colors.grey,
                  borderRadius: BorderRadius.circular(12),
                ),
                child: Row(
                  children: [
                    Container(
                      width: 8,
                      height: 8,
                      decoration: const BoxDecoration(
                        color: Colors.white,
                        shape: BoxShape.circle,
                      ),
                    ),
                    const SizedBox(width: 8),
                    Text(
                      server.isRunning ? 'En ligne' : 'Hors ligne',
                      style: const TextStyle(
                        color: Colors.white,
                        fontSize: 12,
                        fontWeight: FontWeight.w600,
                      ),
                    ),
                  ],
                ),
              ),
            ],
          ),
          leading: IconButton(
            icon: const Icon(FluentIcons.back),
            onPressed: () => Navigator.of(context).pop(),
          ),
          actions: Row(
            children: [
              Text(
                'Version: ${server.version}',
                style: const TextStyle(fontSize: 12),
              ),
              const SizedBox(width: 16),
              Text(
                'Playlist: ${server.playlist}',
                style: const TextStyle(fontSize: 12),
              ),
              const SizedBox(width: 16),
            ],
          ),
        ),
        pane: NavigationPane(
          selected: _selectedIndex,
          onChanged: (index) => setState(() => _selectedIndex = index),
          displayMode: PaneDisplayMode.compact,
          items: [
            PaneItem(
              icon: const Icon(FluentIcons.play_solid),
              title: const Text('Contrôle'),
              body: const ServerControlPanel(),
            ),
            PaneItem(
              icon: const Icon(FluentIcons.plugin),
              title: const Text('DLLs'),
              body: const DllSelectorPanel(),
            ),
            PaneItem(
              icon: const Icon(FluentIcons.page_list),
              title: const Text('Logs'),
              body: const LogsPanel(),
            ),
            PaneItem(
              icon: const Icon(FluentIcons.shield),
              title: const Text('Modération'),
              body: const ModerationPanel(),
            ),
            PaneItem(
              icon: const Icon(FluentIcons.bar_chart_vertical),
              title: const Text('Statistiques'),
              body: const StatisticsPanel(),
            ),
          ],
        ),
      );
    });
  }
}
