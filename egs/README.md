# EGS - Epic Game Server

**EGS** est un logiciel de gestion de serveurs de jeu Fortnite, conçu pour faciliter la création, la configuration et la gestion de serveurs privés.

## Fonctionnalités

### 🎮 Gestion de serveurs
- **Création facile** : Interface intuitive pour créer de nouveaux serveurs
- **Configuration complète** : Nom, mot de passe, version Fortnite, playlist, port, etc.
- **Multi-serveurs** : Gérez plusieurs configurations de serveurs
- **Sauvegarde automatique** : Toutes les configurations sont sauvegardées

### 🎛️ Contrôle du serveur
- **Démarrer/Arrêter/Redémarrer** : Contrôle complet du serveur
- **Monitoring en temps réel** : Visualisation de l'état du serveur
- **PID tracking** : Suivi du processus du serveur

### 🔌 Gestion des DLLs
- **Sélection de DLLs** : Ajoutez et configurez vos DLLs personnalisées
- **Injection automatique** : Injectez des DLLs dans le processus du serveur
- **DLLs recommandées** : Liste des DLLs couramment utilisées
- **Vérification** : Détection automatique des DLLs manquantes

### 📊 Logs en temps réel
- **Console intégrée** : Visualisation des logs du serveur en direct
- **Coloration syntaxique** : Logs colorés selon le niveau (erreur, info, etc.)
- **Export** : Possibilité d'effacer et de filtrer les logs
- **Suivi du processus** : Affichage de stdout et stderr

### 🛡️ Modération
- **Gestion des joueurs** : Liste des joueurs connectés
- **Kick/Ban** : Expulsez ou bannissez des joueurs
- **Messages globaux** : Envoyez des messages à tous les joueurs
- **Historique** : Liste des joueurs bannis avec possibilité de débannir

### 📈 Statistiques
- **Temps de fonctionnement** : Uptime du serveur
- **Statistiques joueurs** : Joueurs actuels, record, total
- **Ressources système** : Utilisation CPU et mémoire
- **Réseau** : Trafic entrant et sortant
- **Informations détaillées** : Configuration complète du serveur

## Installation

### Prérequis
- Windows 10/11
- Flutter SDK 3.19.0 ou inférieur
- Dart SDK
- Fortnite installé

### Compilation

1. Clonez le dépôt :
```bash
cd egs
```

2. Installez les dépendances :
```bash
flutter pub get
```

3. Compilez l'application :
```bash
flutter build windows
```

4. L'exécutable se trouvera dans `build/windows/runner/Release/egs.exe`

## Utilisation

### Créer un serveur

1. Lancez **EGS**
2. Cliquez sur "Nouveau serveur" ou le bouton **+**
3. Remplissez les informations :
   - **Nom** : Nom de votre serveur
   - **Mot de passe** : (Optionnel) Mot de passe pour protéger le serveur
   - **Version** : Sélectionnez la version de Fortnite
   - **Playlist** : Choisissez le mode de jeu (Solo, Duo, Squad, etc.)
   - **Chemin du jeu** : Sélectionnez l'exécutable `FortniteClient-Win64-Shipping.exe`
   - **Port** : Port du serveur (par défaut 7777)
   - **Joueurs max** : Nombre maximum de joueurs

4. Cliquez sur "Créer"

### Gérer un serveur

1. Cliquez sur une carte de serveur dans la page d'accueil
2. Vous accédez à l'interface de gestion avec 5 sections :

#### 1. Contrôle
- Démarrez, arrêtez ou redémarrez le serveur
- Consultez les informations du serveur
- Visualisez le statut en temps réel

#### 2. DLLs
- Ajoutez des DLLs personnalisées
- Injectez des DLLs dans le serveur en cours d'exécution
- Consultez les DLLs recommandées

#### 3. Logs
- Visualisez les logs du serveur en temps réel
- Logs colorés selon le type (erreur, info, etc.)
- Effacez les logs si nécessaire

#### 4. Modération
- Gérez les joueurs connectés
- Expulsez (kick) ou bannissez (ban) des joueurs
- Envoyez des messages globaux
- Gérez la liste des bannis

#### 5. Statistiques
- Consultez le temps de fonctionnement
- Visualisez les statistiques de joueurs
- Surveillez l'utilisation des ressources
- Analysez le trafic réseau

### Supprimer un serveur

1. Sur la page d'accueil, cliquez sur l'icône **poubelle** sur la carte du serveur
2. Confirmez la suppression

## Architecture technique

### Structure du projet
```
egs/
├── lib/
│   ├── src/
│   │   ├── controllers/       # Contrôleurs GetX
│   │   │   └── server_controller.dart
│   │   ├── models/           # Modèles de données
│   │   │   └── game_server.dart
│   │   ├── pages/            # Pages principales
│   │   │   ├── home_page.dart
│   │   │   └── server_management_page.dart
│   │   └── widgets/          # Widgets réutilisables
│   │       ├── create_server_dialog.dart
│   │       ├── server_control_panel.dart
│   │       ├── dll_selector_panel.dart
│   │       ├── logs_panel.dart
│   │       ├── moderation_panel.dart
│   │       └── statistics_panel.dart
│   └── main.dart
├── assets/
│   └── icons/
├── pubspec.yaml
└── README.md
```

### Technologies utilisées
- **Flutter** : Framework UI
- **Fluent UI** : Design Windows 11
- **GetX** : Gestion d'état
- **GetStorage** : Stockage local
- **Reboot Common** : Bibliothèque partagée pour la gestion des processus et DLLs

## Roadmap

### Fonctionnalités à venir
- [ ] Détection automatique des joueurs connectés
- [ ] Intégration avec les APIs du serveur Fortnite
- [ ] Graphiques de statistiques en temps réel
- [ ] Export des logs vers fichier
- [ ] Backup/Restore de configurations
- [ ] Support multi-langues
- [ ] Auto-updater
- [ ] Thème clair/sombre personnalisable

## Contribution

Les contributions sont les bienvenues ! N'hésitez pas à :
- Signaler des bugs
- Proposer de nouvelles fonctionnalités
- Soumettre des pull requests

## Licence

Ce projet est sous licence MIT. Voir le fichier LICENSE pour plus de détails.

## Support

Pour toute question ou problème :
- Ouvrez une issue sur GitHub
- Consultez la documentation de Reboot Launcher

---

**Note** : EGS est un outil pour gérer des serveurs privés Fortnite à des fins éducatives et de test. Assurez-vous de respecter les conditions d'utilisation d'Epic Games.
