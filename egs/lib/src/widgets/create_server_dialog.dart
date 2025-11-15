import 'dart:io';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:file_picker/file_picker.dart';
import '../controllers/server_controller.dart';

/// Dialogue pour créer un nouveau serveur
class CreateServerDialog extends StatefulWidget {
  const CreateServerDialog({super.key});

  @override
  State<CreateServerDialog> createState() => _CreateServerDialogState();
}

class _CreateServerDialogState extends State<CreateServerDialog> {
  final _formKey = GlobalKey<FormState>();
  final _nameController = TextEditingController();
  final _passwordController = TextEditingController();
  final _portController = TextEditingController(text: '7777');
  final _maxPlayersController = TextEditingController(text: '100');

  String _selectedVersion = 'Season 3';
  String _selectedPlaylist = 'Solo';
  String _gamePath = '';

  final List<String> _versions = [
    'Season 2',
    'Season 3',
    'Season 4',
    'Season 5',
    'Season 6',
    'Season 7',
    'Season 8',
    'Season 9',
    'Season 10',
    'Season 11',
    'Season 12',
    'Season 13',
    'Season 14',
    'Season 15',
    'Season 16',
    'Season 17',
    'Season 18',
    'Season 19',
    'Season 20',
  ];

  final List<String> _playlists = [
    'Solo',
    'Duo',
    'Squad',
    'Arena Solo',
    'Arena Duo',
    'Arena Trios',
    'Team Rumble',
    'Creative',
    'Playground',
  ];

  @override
  void dispose() {
    _nameController.dispose();
    _passwordController.dispose();
    _portController.dispose();
    _maxPlayersController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return ContentDialog(
      title: const Text('Créer un nouveau serveur'),
      content: SizedBox(
        width: 500,
        child: Form(
          key: _formKey,
          child: ListView(
            shrinkWrap: true,
            children: [
              // Nom du serveur
              InfoLabel(
                label: 'Nom du serveur *',
                child: TextFormBox(
                  controller: _nameController,
                  placeholder: 'Mon serveur Fortnite',
                  validator: (value) {
                    if (value == null || value.isEmpty) {
                      return 'Le nom du serveur est requis';
                    }
                    return null;
                  },
                ),
              ),
              const SizedBox(height: 16),

              // Mot de passe
              InfoLabel(
                label: 'Mot de passe (optionnel)',
                child: TextFormBox(
                  controller: _passwordController,
                  placeholder: 'Laisser vide pour aucun mot de passe',
                  obscureText: true,
                ),
              ),
              const SizedBox(height: 16),

              // Version
              InfoLabel(
                label: 'Version de Fortnite *',
                child: ComboBox<String>(
                  value: _selectedVersion,
                  items: _versions
                      .map((version) => ComboBoxItem(
                            value: version,
                            child: Text(version),
                          ))
                      .toList(),
                  onChanged: (value) {
                    if (value != null) {
                      setState(() => _selectedVersion = value);
                    }
                  },
                ),
              ),
              const SizedBox(height: 16),

              // Playlist
              InfoLabel(
                label: 'Playlist *',
                child: ComboBox<String>(
                  value: _selectedPlaylist,
                  items: _playlists
                      .map((playlist) => ComboBoxItem(
                            value: playlist,
                            child: Text(playlist),
                          ))
                      .toList(),
                  onChanged: (value) {
                    if (value != null) {
                      setState(() => _selectedPlaylist = value);
                    }
                  },
                ),
              ),
              const SizedBox(height: 16),

              // Chemin du jeu
              InfoLabel(
                label: 'Chemin de l\'exécutable du jeu *',
                child: Row(
                  children: [
                    Expanded(
                      child: TextFormBox(
                        placeholder: 'Sélectionner le fichier FortniteClient-Win64-Shipping.exe',
                        readOnly: true,
                        controller: TextEditingController(text: _gamePath),
                        validator: (value) {
                          if (_gamePath.isEmpty) {
                            return 'Le chemin du jeu est requis';
                          }
                          if (!File(_gamePath).existsSync()) {
                            return 'Le fichier n\'existe pas';
                          }
                          return null;
                        },
                      ),
                    ),
                    const SizedBox(width: 8),
                    Button(
                      onPressed: _selectGamePath,
                      child: const Text('Parcourir'),
                    ),
                  ],
                ),
              ),
              const SizedBox(height: 16),

              // Port
              InfoLabel(
                label: 'Port *',
                child: TextFormBox(
                  controller: _portController,
                  placeholder: '7777',
                  validator: (value) {
                    if (value == null || value.isEmpty) {
                      return 'Le port est requis';
                    }
                    final port = int.tryParse(value);
                    if (port == null || port < 1 || port > 65535) {
                      return 'Port invalide (1-65535)';
                    }
                    return null;
                  },
                ),
              ),
              const SizedBox(height: 16),

              // Nombre max de joueurs
              InfoLabel(
                label: 'Nombre maximum de joueurs *',
                child: TextFormBox(
                  controller: _maxPlayersController,
                  placeholder: '100',
                  validator: (value) {
                    if (value == null || value.isEmpty) {
                      return 'Le nombre de joueurs est requis';
                    }
                    final maxPlayers = int.tryParse(value);
                    if (maxPlayers == null || maxPlayers < 1) {
                      return 'Nombre invalide';
                    }
                    return null;
                  },
                ),
              ),
            ],
          ),
        ),
      ),
      actions: [
        Button(
          onPressed: () => Navigator.of(context).pop(),
          child: const Text('Annuler'),
        ),
        FilledButton(
          onPressed: _createServer,
          child: const Text('Créer'),
        ),
      ],
    );
  }

  Future<void> _selectGamePath() async {
    final result = await FilePicker.platform.pickFiles(
      type: FileType.custom,
      allowedExtensions: ['exe'],
      dialogTitle: 'Sélectionner FortniteClient-Win64-Shipping.exe',
    );

    if (result != null && result.files.isNotEmpty) {
      setState(() {
        _gamePath = result.files.first.path!;
      });
    }
  }

  Future<void> _createServer() async {
    if (!_formKey.currentState!.validate()) {
      return;
    }

    final ServerController controller = Get.find();

    await controller.createServer(
      name: _nameController.text,
      password: _passwordController.text,
      version: _selectedVersion,
      playlist: _selectedPlaylist,
      gamePath: _gamePath,
      port: int.parse(_portController.text),
      maxPlayers: int.parse(_maxPlayersController.text),
    );

    if (context.mounted) {
      Navigator.of(context).pop();
    }
  }
}
