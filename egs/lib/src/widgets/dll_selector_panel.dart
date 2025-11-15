import 'dart:io';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:file_picker/file_picker.dart';
import '../controllers/server_controller.dart';

/// Panneau de sélection des DLLs à injecter
class DllSelectorPanel extends StatefulWidget {
  const DllSelectorPanel({super.key});

  @override
  State<DllSelectorPanel> createState() => _DllSelectorPanelState();
}

class _DllSelectorPanelState extends State<DllSelectorPanel> {
  @override
  Widget build(BuildContext context) {
    final ServerController controller = Get.find();

    return ScaffoldPage.scrollable(
      header: const PageHeader(
        title: Text('Gestion des DLLs'),
      ),
      children: [
        Obx(() {
          final server = controller.selectedServer.value;
          if (server == null) return const SizedBox();

          return Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              // Liste des DLLs sélectionnées
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(16.0),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          const Text(
                            'DLLs configurées',
                            style: TextStyle(
                              fontSize: 18,
                              fontWeight: FontWeight.bold,
                            ),
                          ),
                          FilledButton(
                            onPressed: () => _addDll(context, controller, server),
                            child: Row(
                              mainAxisSize: MainAxisSize.min,
                              children: const [
                                Icon(FluentIcons.add, size: 16),
                                SizedBox(width: 8),
                                Text('Ajouter DLL'),
                              ],
                            ),
                          ),
                        ],
                      ),
                      const SizedBox(height: 16),
                      if (server.selectedDlls.isEmpty)
                        const Center(
                          child: Padding(
                            padding: EdgeInsets.all(32.0),
                            child: Text(
                              'Aucune DLL configurée',
                              style: TextStyle(fontSize: 14),
                            ),
                          ),
                        )
                      else
                        ListView.builder(
                          shrinkWrap: true,
                          physics: const NeverScrollableScrollPhysics(),
                          itemCount: server.selectedDlls.length,
                          itemBuilder: (context, index) {
                            final entry = server.selectedDlls.entries.elementAt(index);
                            return _DllListItem(
                              name: entry.key,
                              path: entry.value,
                              onRemove: () => _removeDll(controller, server, entry.key),
                              onInject: server.isRunning
                                  ? () => _injectDll(context, controller, server, entry.value)
                                  : null,
                            );
                          },
                        ),
                    ],
                  ),
                ),
              ),

              const SizedBox(height: 24),

              // DLLs recommandées
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(16.0),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      const Text(
                        'DLLs recommandées',
                        style: TextStyle(
                          fontSize: 18,
                          fontWeight: FontWeight.bold,
                        ),
                      ),
                      const SizedBox(height: 16),
                      _RecommendedDllItem(
                        name: 'Console DLL',
                        description: 'Active la console de développement dans le jeu',
                        fileName: 'console.dll',
                      ),
                      const SizedBox(height: 12),
                      _RecommendedDllItem(
                        name: 'Reboot DLL',
                        description: 'DLL principale du serveur Reboot',
                        fileName: 'reboot.dll',
                      ),
                      const SizedBox(height: 12),
                      _RecommendedDllItem(
                        name: 'Memory Leak Fix',
                        description: 'Corrige les fuites mémoire du serveur',
                        fileName: 'memory.dll',
                      ),
                      const SizedBox(height: 12),
                      _RecommendedDllItem(
                        name: 'Auth Backend DLL',
                        description: 'DLL pour le backend d\'authentification',
                        fileName: 'sinum.dll',
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

  Future<void> _addDll(
    BuildContext context,
    ServerController controller,
    server,
  ) async {
    final result = await FilePicker.platform.pickFiles(
      type: FileType.custom,
      allowedExtensions: ['dll'],
      dialogTitle: 'Sélectionner une DLL',
    );

    if (result != null && result.files.isNotEmpty) {
      final filePath = result.files.first.path!;
      final fileName = result.files.first.name;

      final nameController = TextEditingController(
        text: fileName.replaceAll('.dll', ''),
      );

      if (context.mounted) {
        final name = await showDialog<String>(
          context: context,
          builder: (context) => ContentDialog(
            title: const Text('Nom de la DLL'),
            content: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Text('Entrez un nom pour identifier cette DLL:'),
                const SizedBox(height: 16),
                TextBox(
                  controller: nameController,
                  placeholder: 'Nom de la DLL',
                ),
              ],
            ),
            actions: [
              Button(
                onPressed: () => Navigator.of(context).pop(),
                child: const Text('Annuler'),
              ),
              FilledButton(
                onPressed: () => Navigator.of(context).pop(nameController.text),
                child: const Text('Ajouter'),
              ),
            ],
          ),
        );

        if (name != null && name.isNotEmpty) {
          final updatedDlls = Map<String, String>.from(server.selectedDlls);
          updatedDlls[name] = filePath;

          final updatedServer = server.copyWith(selectedDlls: updatedDlls);
          await controller.updateServer(updatedServer);

          if (context.mounted) {
            displayInfoBar(
              context,
              builder: (context, close) => const InfoBar(
                title: Text('DLL ajoutée'),
                severity: InfoBarSeverity.success,
              ),
            );
          }
        }
      }
    }
  }

  Future<void> _removeDll(
    ServerController controller,
    server,
    String name,
  ) async {
    final updatedDlls = Map<String, String>.from(server.selectedDlls);
    updatedDlls.remove(name);

    final updatedServer = server.copyWith(selectedDlls: updatedDlls);
    await controller.updateServer(updatedServer);
  }

  Future<void> _injectDll(
    BuildContext context,
    ServerController controller,
    server,
    String dllPath,
  ) async {
    final success = await controller.injectDll(server, dllPath);

    if (context.mounted) {
      displayInfoBar(
        context,
        builder: (context, close) => InfoBar(
          title: Text(
            success ? 'DLL injectée' : 'Échec de l\'injection',
          ),
          severity: success ? InfoBarSeverity.success : InfoBarSeverity.error,
        ),
      );
    }
  }
}

class _DllListItem extends StatelessWidget {
  final String name;
  final String path;
  final VoidCallback onRemove;
  final VoidCallback? onInject;

  const _DllListItem({
    required this.name,
    required this.path,
    required this.onRemove,
    this.onInject,
  });

  @override
  Widget build(BuildContext context) {
    final exists = File(path).existsSync();

    return Container(
      margin: const EdgeInsets.only(bottom: 8),
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        border: Border.all(color: Colors.grey[60]),
        borderRadius: BorderRadius.circular(4),
      ),
      child: Row(
        children: [
          Icon(
            exists ? FluentIcons.plugin : FluentIcons.warning,
            color: exists ? Colors.blue : Colors.orange,
            size: 20,
          ),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  name,
                  style: const TextStyle(
                    fontWeight: FontWeight.w600,
                    fontSize: 14,
                  ),
                ),
                const SizedBox(height: 4),
                Text(
                  path,
                  style: TextStyle(
                    fontSize: 12,
                    color: Colors.grey[100],
                  ),
                  overflow: TextOverflow.ellipsis,
                ),
                if (!exists)
                  const Text(
                    'Le fichier n\'existe pas',
                    style: TextStyle(
                      fontSize: 11,
                      color: Colors.orange,
                    ),
                  ),
              ],
            ),
          ),
          if (onInject != null && exists)
            Button(
              onPressed: onInject,
              child: const Text('Injecter'),
            ),
          const SizedBox(width: 8),
          IconButton(
            icon: const Icon(FluentIcons.delete),
            onPressed: onRemove,
          ),
        ],
      ),
    );
  }
}

class _RecommendedDllItem extends StatelessWidget {
  final String name;
  final String description;
  final String fileName;

  const _RecommendedDllItem({
    required this.name,
    required this.description,
    required this.fileName,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        border: Border.all(color: Colors.grey[60]),
        borderRadius: BorderRadius.circular(4),
      ),
      child: Row(
        children: [
          const Icon(FluentIcons.info, size: 20),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  name,
                  style: const TextStyle(
                    fontWeight: FontWeight.w600,
                    fontSize: 14,
                  ),
                ),
                const SizedBox(height: 4),
                Text(
                  description,
                  style: const TextStyle(fontSize: 12),
                ),
                const SizedBox(height: 4),
                Text(
                  'Fichier: $fileName',
                  style: TextStyle(
                    fontSize: 11,
                    color: Colors.grey[100],
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}
