# Guide de démarrage rapide - EGS

## Installation rapide

### 1. Installer les dépendances

```bash
cd egs
flutter pub get
```

### 2. Lancer en mode développement

```bash
flutter run -d windows
```

### 3. Compiler pour la production

```bash
flutter build windows --release
```

L'exécutable sera disponible dans : `build/windows/runner/Release/egs.exe`

## Premier lancement

### Étape 1 : Créer votre premier serveur

1. Lancez **EGS**
2. Cliquez sur **"Nouveau serveur"**
3. Remplissez le formulaire :
   ```
   Nom : Mon Premier Serveur
   Mot de passe : (laissez vide pour un serveur public)
   Version : Season 3
   Playlist : Solo
   Chemin du jeu : C:\...\FortniteClient-Win64-Shipping.exe
   Port : 7777
   Joueurs max : 100
   ```
4. Cliquez sur **"Créer"**

### Étape 2 : Configurer les DLLs (Optionnel)

1. Cliquez sur votre serveur
2. Allez dans l'onglet **"DLLs"**
3. Cliquez sur **"Ajouter DLL"**
4. Sélectionnez vos DLLs (console.dll, reboot.dll, etc.)
5. Donnez un nom à chaque DLL

### Étape 3 : Démarrer le serveur

1. Allez dans l'onglet **"Contrôle"**
2. Cliquez sur **"Démarrer"**
3. Le serveur se lance !

### Étape 4 : Surveiller les logs

1. Allez dans l'onglet **"Logs"**
2. Visualisez les logs en temps réel
3. Les erreurs apparaissent en rouge
4. Les logs stdout apparaissent en vert

### Étape 5 : Gérer les joueurs

1. Allez dans l'onglet **"Modération"**
2. Vous verrez les joueurs connectés (une fois implémenté)
3. Vous pouvez kick ou ban des joueurs
4. Envoyez des messages globaux

### Étape 6 : Consulter les statistiques

1. Allez dans l'onglet **"Statistiques"**
2. Consultez le temps de fonctionnement
3. Visualisez les statistiques de joueurs
4. Surveillez les ressources système

## Raccourcis clavier

- `Ctrl + N` : Nouveau serveur (à implémenter)
- `Ctrl + S` : Sauvegarder (automatique)
- `Ctrl + Q` : Quitter

## Dépannage

### Le serveur ne démarre pas
- Vérifiez que le chemin du jeu est correct
- Assurez-vous que le port n'est pas déjà utilisé
- Consultez les logs pour voir les erreurs

### Les DLLs ne s'injectent pas
- Le serveur doit être en cours d'exécution
- Vérifiez que le fichier DLL existe
- Consultez les logs pour voir les erreurs d'injection

### La fenêtre ne s'affiche pas
- Vérifiez que vous êtes sur Windows 10/11
- Assurez-vous d'avoir les derniers pilotes graphiques

## Support

Pour plus d'informations, consultez le README.md complet.
