import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import '../controllers/server_controller.dart';
import '../models/game_server.dart';
import '../widgets/create_server_dialog.dart';
import 'server_management_page.dart';

/// Page d'accueil affichant la liste des serveurs
class HomePage extends StatelessWidget {
  const HomePage({super.key});

  @override
  Widget build(BuildContext context) {
    final ServerController controller = Get.find();

    return ScaffoldPage(
      header: PageHeader(
        title: const Text('EGS - Epic Game Server'),
        commandBar: CommandBar(
          primaryItems: [
            CommandBarButton(
              icon: const Icon(FluentIcons.add),
              label: const Text('Nouveau serveur'),
              onPressed: () => _showCreateServerDialog(context),
            ),
          ],
        ),
      ),
      content: Padding(
        padding: const EdgeInsets.all(16.0),
        child: Obx(() {
          if (controller.servers.isEmpty) {
            return Center(
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  const Icon(
                    FluentIcons.server,
                    size: 64,
                  ),
                  const SizedBox(height: 16),
                  const Text(
                    'Aucun serveur configuré',
                    style: TextStyle(fontSize: 20, fontWeight: FontWeight.bold),
                  ),
                  const SizedBox(height: 8),
                  const Text('Créez votre premier serveur pour commencer'),
                  const SizedBox(height: 24),
                  FilledButton(
                    onPressed: () => _showCreateServerDialog(context),
                    child: const Text('Créer un serveur'),
                  ),
                ],
              ),
            );
          }

          return GridView.builder(
            gridDelegate: const SliverGridDelegateWithMaxCrossAxisExtent(
              maxCrossAxisExtent: 350,
              childAspectRatio: 1.5,
              crossAxisSpacing: 16,
              mainAxisSpacing: 16,
            ),
            itemCount: controller.servers.length,
            itemBuilder: (context, index) {
              final server = controller.servers[index];
              return _ServerCard(
                server: server,
                onTap: () => _openServerManagement(context, server),
                onDelete: () => _confirmDelete(context, server),
              );
            },
          );
        }),
      ),
    );
  }

  void _showCreateServerDialog(BuildContext context) {
    showDialog(
      context: context,
      builder: (context) => const CreateServerDialog(),
    );
  }

  void _openServerManagement(BuildContext context, GameServer server) {
    final ServerController controller = Get.find();
    controller.selectServer(server);

    Navigator.of(context).push(
      FluentPageRoute(
        builder: (context) => const ServerManagementPage(),
      ),
    );
  }

  void _confirmDelete(BuildContext context, GameServer server) async {
    final result = await showDialog<bool>(
      context: context,
      builder: (context) => ContentDialog(
        title: const Text('Confirmer la suppression'),
        content: Text('Voulez-vous vraiment supprimer le serveur "${server.name}" ?'),
        actions: [
          Button(
            onPressed: () => Navigator.of(context).pop(false),
            child: const Text('Annuler'),
          ),
          FilledButton(
            onPressed: () => Navigator.of(context).pop(true),
            child: const Text('Supprimer'),
          ),
        ],
      ),
    );

    if (result == true) {
      final ServerController controller = Get.find();
      await controller.deleteServer(server.id);
    }
  }
}

/// Carte affichant les informations d'un serveur
class _ServerCard extends StatelessWidget {
  final GameServer server;
  final VoidCallback onTap;
  final VoidCallback onDelete;

  const _ServerCard({
    required this.server,
    required this.onTap,
    required this.onDelete,
  });

  @override
  Widget build(BuildContext context) {
    return Card(
      child: InkWell(
        onTap: onTap,
        child: Padding(
          padding: const EdgeInsets.all(16.0),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Expanded(
                    child: Text(
                      server.name,
                      style: const TextStyle(
                        fontSize: 18,
                        fontWeight: FontWeight.bold,
                      ),
                      overflow: TextOverflow.ellipsis,
                    ),
                  ),
                  IconButton(
                    icon: const Icon(FluentIcons.delete, size: 16),
                    onPressed: onDelete,
                  ),
                ],
              ),
              const SizedBox(height: 12),
              _InfoRow(
                icon: FluentIcons.game,
                label: 'Version',
                value: server.version,
              ),
              const SizedBox(height: 4),
              _InfoRow(
                icon: FluentIcons.list,
                label: 'Playlist',
                value: server.playlist,
              ),
              const SizedBox(height: 4),
              _InfoRow(
                icon: FluentIcons.plug_connected,
                label: 'Port',
                value: server.port.toString(),
              ),
              const Spacer(),
              Row(
                children: [
                  Container(
                    width: 8,
                    height: 8,
                    decoration: BoxDecoration(
                      color: server.isRunning ? Colors.green : Colors.grey,
                      shape: BoxShape.circle,
                    ),
                  ),
                  const SizedBox(width: 8),
                  Text(
                    server.isRunning ? 'En ligne' : 'Hors ligne',
                    style: TextStyle(
                      color: server.isRunning ? Colors.green : Colors.grey,
                      fontWeight: FontWeight.w500,
                    ),
                  ),
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }
}

/// Widget pour afficher une ligne d'information
class _InfoRow extends StatelessWidget {
  final IconData icon;
  final String label;
  final String value;

  const _InfoRow({
    required this.icon,
    required this.label,
    required this.value,
  });

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Icon(icon, size: 14),
        const SizedBox(width: 8),
        Text(
          '$label: ',
          style: const TextStyle(fontSize: 12, fontWeight: FontWeight.w500),
        ),
        Expanded(
          child: Text(
            value,
            style: const TextStyle(fontSize: 12),
            overflow: TextOverflow.ellipsis,
          ),
        ),
      ],
    );
  }
}
