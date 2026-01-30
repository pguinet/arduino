# HelloWorld - Premier sketch pour JC3248W535C

Sketch de test minimal pour vérifier que l'afficheur LCD fonctionne correctement.

## Description

Ce sketch affiche un simple message "Hello World!" au centre de l'écran en vert avec une grande police.

C'est le premier sketch à compiler et tester pour vérifier que toute la configuration matérielle et logicielle est correcte.

## Prérequis

### Matériel
- Carte JC3248W535C (ESP32-S3 avec écran 3.5")
- Câble USB-C pour la programmation

### Logiciel

#### 1. ESP32 Arduino Core v3.0.2
⚠️ **CRITIQUE** : La version v3.0.2 est obligatoire.

Installation via le gestionnaire de cartes Arduino :
1. Fichier > Préférences
2. URLs de gestionnaire de cartes supplémentaires :
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
3. Outils > Type de carte > Gestionnaire de carte
4. Rechercher "esp32" et installer version **3.0.2**

#### 2. Bibliothèque LVGL

La bibliothèque LVGL doit être installée dans ton dossier `libraries/` Arduino.

**Option 1 : Copier depuis la doc vendeur**
```bash
cp -r doc/JC3248W535EN/1-Demo/Demo_Arduino/DEMO_LVGL/libraries/lvgl ~/Arduino/libraries/
```

**Option 2 : Gestionnaire de bibliothèques Arduino**
1. Croquis > Inclure une bibliothèque > Gérer les bibliothèques
2. Rechercher "lvgl"
3. Installer LVGL version 8.x (testé avec 8.3.x)

#### 3. Fichiers lib/

Les fichiers dans `../lib/` doivent être présents :
- esp_bsp.c / esp_bsp.h
- esp_lcd_axs15231b.c / esp_lcd_axs15231b.h
- esp_lcd_touch.c / esp_lcd_touch.h
- lv_port.c / lv_port.h
- display.h
- lv_conf.h
- bsp_err_check.h

Ces fichiers sont déjà extraits de la doc vendeur et versionnés.

## Configuration Arduino IDE

### Board Settings

```
Board: "ESP32S3 Dev Module"
Upload Speed: "921600"
USB Mode: "Hardware CDC and JTAG"
USB CDC On Boot: "Enabled"
USB Firmware MSC On Boot: "Disabled"
USB DFD On Boot: "Disabled"
Upload Mode: "UART0 / Hardware CDC"
CPU Frequency: "240MHz (WiFi)"
Flash Mode: "QIO 80MHz"
Flash Size: "16MB (128Mb)"
Partition Scheme: "16M Flash (3MB APP/9.9MB FATFS)"
Core Debug Level: "None"
PSRAM: "QSPI PSRAM"
PSRAM Speed: "120MHz"
Arduino Runs On: "Core 1"
Events Run On: "Core 1"
```

### Réglages critiques

Les paramètres suivants sont **obligatoires** :
- ✅ ESP32 Core : **v3.0.2**
- ✅ PSRAM : **QSPI PSRAM**
- ✅ PSRAM Speed : **120MHz**
- ✅ Partition : Au moins **3MB APP**

## Compilation et Upload

### 1. Ouvrir le sketch
```
Fichier > Ouvrir > HelloWorld.ino
```

### 2. Vérifier la configuration
- Vérifier que tous les réglages ci-dessus sont corrects
- Vérifier que le port série est sélectionné

### 3. Compiler
```
Croquis > Vérifier/Compiler
```

**Taille attendue** : ~1.5 MB

### 4. Uploader
```
Croquis > Téléverser
```

## Résultat attendu

### Console série (115200 baud)

```
HelloWorld - JC3248W535C - Démarrage
Initialisation de l'afficheur...
Initialize SPI bus
Install panel IO
Install LCD driver of axs15231b
LCD panel create success, version: 1.0.0
Setting LCD backlight: 100%
Création de l'interface...
HelloWorld - JC3248W535C - Initialisé avec succès !
L'afficheur devrait maintenant afficher 'Hello World!'
```

### Écran LCD

L'écran doit afficher en mode paysage (rotation 90°) :

```
┌─────────────────────────────┐
│                             │
│        Hello World!         │
│                             │
│       JC3248W535C           │
│         ESP32-S3            │
│                             │
│                             │
└─────────────────────────────┘
```

Texte en **vert** (`0x00FF00`), police **grande** (Montserrat 32), **centré**.

## Dépannage

### Problème : Écran reste noir

**Solutions possibles** :

1. **Backlight non allumé**
   - Vérifier que `bsp_display_backlight_on()` est appelé
   - Vérifier GPIO 1 (backlight PWM)

2. **PSRAM non activé**
   - Vérifier : Outils > PSRAM > "QSPI PSRAM"
   - Recompiler et uploader

3. **Mauvaise version ESP32 Core**
   - Vérifier la version : doit être **v3.0.2**
   - Désinstaller les autres versions
   - Réinstaller v3.0.2

4. **Initialisation échouée**
   - Ouvrir le moniteur série (115200 baud)
   - Chercher des messages d'erreur
   - Vérifier les codes d'erreur ESP_ERR_*

### Problème : Erreur de compilation

**Solutions possibles** :

1. **LVGL non trouvée**
   ```
   fatal error: lvgl.h: No such file or directory
   ```
   → Installer la bibliothèque LVGL (voir section Prérequis)

2. **Fichiers lib/ manquants**
   ```
   fatal error: ../lib/esp_bsp.h: No such file or directory
   ```
   → Vérifier que le dossier `../lib/` existe avec tous les fichiers

3. **Partition trop petite**
   ```
   Sketch too big
   ```
   → Changer Partition Scheme vers "3MB APP" ou plus

4. **Symboles non définis**
   ```
   undefined reference to `bsp_display_start_with_config'
   ```
   → Vérifier que les fichiers .c sont bien dans lib/
   → Arduino IDE doit voir les .c et les compiler

### Problème : Upload échoue

**Solutions possibles** :

1. **Port série non reconnu**
   - Débrancher/rebrancher le câble USB
   - Maintenir le bouton BOOT pendant l'upload

2. **Timeout**
   - Réduire Upload Speed à 460800 ou 115200
   - Essayer un autre câble USB

3. **Permission denied (Linux)**
   ```bash
   sudo usermod -a -G dialout $USER
   # Puis déconnecter/reconnecter la session
   ```

## Personnalisation

### Changer la rotation

Modifier la constante dans le .ino :
```cpp
#define LVGL_PORT_ROTATION_DEGREE (90)  // 0, 90, 180, ou 270
```

### Changer le texte

Modifier dans `setup()` :
```cpp
lv_label_set_text(label, "Ton texte ici!");
```

### Changer la couleur

```cpp
lv_obj_set_style_text_color(label, lv_color_hex(0xFF0000), 0);  // Rouge
```

Couleurs disponibles :
- `0xFF0000` : Rouge
- `0x00FF00` : Vert
- `0x0000FF` : Bleu
- `0xFFFF00` : Jaune
- `0xFF00FF` : Magenta
- `0x00FFFF` : Cyan
- `0xFFFFFF` : Blanc

### Changer la taille de police

LVGL fournit plusieurs polices par défaut :
```cpp
lv_style_set_text_font(&style_large, &lv_font_montserrat_16);  // 16px
lv_style_set_text_font(&style_large, &lv_font_montserrat_20);  // 20px
lv_style_set_text_font(&style_large, &lv_font_montserrat_24);  // 24px
lv_style_set_text_font(&style_large, &lv_font_montserrat_32);  // 32px
```

## Prochaines étapes

Une fois que ce sketch fonctionne :

1. ✅ L'afficheur est confirmé fonctionnel
2. ✅ La configuration Arduino IDE est correcte
3. ✅ Les drivers sont bien compilés

Tu peux maintenant :
- Tester le tactile
- Créer des interfaces plus complexes avec LVGL
- Utiliser les autres exemples du vendeur (PIC, MJPEG, MP3)

## Ressources

- [Documentation LVGL](https://docs.lvgl.io/)
- [Exemples LVGL](https://docs.lvgl.io/master/examples.html)
- [Guide d'initialisation complet](../GUIDE_INITIALISATION_AFFICHEUR.md)
- [README bibliothèque lib/](../lib/README.md)

## Licence

Code basé sur les exemples fournis dans la documentation vendeur JC3248W535C.
