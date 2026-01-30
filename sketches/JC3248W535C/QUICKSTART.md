# Démarrage rapide - JC3248W535C

Guide ultra-rapide pour faire fonctionner l'afficheur en 5 minutes.

## Prérequis immédiats

- ✅ Carte JC3248W535C
- ✅ Câble USB-C
- ✅ Arduino IDE installé

## Étape 1 : Installer ESP32 Core v3.0.2

⚠️ **Version exacte requise : v3.0.2**

1. Arduino IDE > Fichier > Préférences
2. URLs de gestionnaire de cartes :
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
3. Outils > Type de carte > Gestionnaire de carte
4. Chercher "esp32" → Installer **version 3.0.2**

## Étape 2 : Installer LVGL

**Option rapide** : Gestionnaire de bibliothèques Arduino

1. Croquis > Inclure une bibliothèque > Gérer les bibliothèques
2. Chercher "lvgl" → Installer version 8.3.x
3. Copier le fichier de config :
   ```bash
   cp lib/lv_conf.h ~/Arduino/libraries/lvgl/
   ```

**Ou** voir [INSTALLATION_LVGL.md](./INSTALLATION_LVGL.md) pour toutes les options.

## Étape 3 : Configurer Arduino IDE

### Board Settings
```
Board: "ESP32S3 Dev Module"
PSRAM: "QSPI PSRAM"               ⚠️ CRITIQUE
PSRAM Speed: "120MHz"              ⚠️ CRITIQUE
Partition Scheme: "16M Flash (3MB APP/9.9MB FATFS)"
CPU Frequency: "240MHz (WiFi)"
Upload Speed: "921600"
USB CDC On Boot: "Enabled"
```

## Étape 4 : Ouvrir et compiler HelloWorld

1. Ouvrir `HelloWorld/HelloWorld.ino`
2. Vérifier le port série : Outils > Port
3. Croquis > Vérifier/Compiler
4. Croquis > Téléverser

## Étape 5 : Vérifier le résultat

### Console série (115200 baud)
```
HelloWorld - JC3248W535C - Démarrage
Initialisation de l'afficheur...
...
HelloWorld - JC3248W535C - Initialisé avec succès !
```

### Écran LCD
Texte vert "Hello World!" centré sur fond noir.

## ✅ Ça marche !

Si l'écran affiche le message, tout est correct !

**Prochaines étapes** :
- Lire [HelloWorld/README.md](./HelloWorld/README.md) pour personnaliser
- Explorer les [exemples LVGL](https://docs.lvgl.io/master/examples.html)
- Tester les démos du vendeur dans `doc/`

## ❌ Ça ne marche pas ?

### Écran noir
1. Vérifier PSRAM : Outils > PSRAM > "QSPI PSRAM"
2. Vérifier PSRAM Speed : "120MHz"
3. Ouvrir le moniteur série (115200 baud) pour voir les erreurs

### Erreur compilation
1. **LVGL not found** → Installer LVGL (Étape 2)
2. **lv_conf.h not found** → Copier lv_conf.h dans libraries/lvgl/
3. **Sketch too big** → Partition Scheme "3MB APP" minimum

### Erreur upload
1. Débrancher/rebrancher USB
2. Maintenir bouton BOOT pendant upload
3. Réduire Upload Speed à 115200

## 📚 Documentation complète

- **[README.md](./README.md)** : Caractéristiques complètes
- **[GUIDE_INITIALISATION_AFFICHEUR.md](./GUIDE_INITIALISATION_AFFICHEUR.md)** : Guide technique détaillé
- **[INSTALLATION_LVGL.md](./INSTALLATION_LVGL.md)** : Installation LVGL
- **[lib/README.md](./lib/README.md)** : Documentation des drivers
- **[HelloWorld/README.md](./HelloWorld/README.md)** : Guide complet du sketch

## Support

En cas de problème :
1. Lire la documentation ci-dessus
2. Vérifier PSRAM et PSRAM Speed (paramètres critiques)
3. Vérifier ESP32 Core v3.0.2
4. Consulter les logs série pour identifier l'erreur

Bon codage ! 🚀
