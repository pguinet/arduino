# Installation de la bibliothèque LVGL

La bibliothèque LVGL (Light and Versatile Graphics Library) est **obligatoire** pour faire fonctionner l'afficheur de la carte JC3248W535C.

## Méthode 1 : Copier depuis la documentation vendeur (Recommandé)

C'est la méthode recommandée car elle utilise la version exacte testée et configurée pour cette carte.

### Étapes

1. **Localiser le dossier LVGL dans la doc vendeur**
   ```
   doc/JC3248W535EN/1-Demo/Demo_Arduino/DEMO_LVGL/libraries/lvgl/
   ```

2. **Copier vers le dossier libraries Arduino**

   **Linux / macOS** :
   ```bash
   # Depuis le dossier racine du projet arduino
   cp -r sketches/JC3248W535C/doc/JC3248W535EN/1-Demo/Demo_Arduino/DEMO_LVGL/libraries/lvgl ~/Arduino/libraries/
   ```

   **Windows** (PowerShell) :
   ```powershell
   Copy-Item -Recurse "sketches\JC3248W535C\doc\JC3248W535EN\1-Demo\Demo_Arduino\DEMO_LVGL\libraries\lvgl" "$env:USERPROFILE\Documents\Arduino\libraries\"
   ```

   **Windows** (Explorateur de fichiers) :
   - Naviguer vers `sketches\JC3248W535C\doc\JC3248W535EN\1-Demo\Demo_Arduino\DEMO_LVGL\libraries\`
   - Copier le dossier `lvgl`
   - Coller dans `Documents\Arduino\libraries\`

3. **Vérifier l'installation**

   Après le redémarrage de l'IDE Arduino, vérifier que LVGL apparaît dans :
   ```
   Croquis > Inclure une bibliothèque > lvgl
   ```

## Méthode 2 : Gestionnaire de bibliothèques Arduino

⚠️ **Attention** : Cette méthode installe la dernière version de LVGL, qui peut ne pas être la même que celle testée avec la carte.

### Étapes

1. **Ouvrir le gestionnaire de bibliothèques**
   ```
   Croquis > Inclure une bibliothèque > Gérer les bibliothèques...
   ```

2. **Rechercher LVGL**
   - Dans la barre de recherche, taper "lvgl"
   - Localiser "lvgl by kisvegabor"

3. **Installer la version 8.x**
   - Sélectionner une version 8.x (par exemple 8.3.x)
   - Cliquer sur "Installer"
   - Accepter d'installer les dépendances si demandé

4. **Copier lv_conf.h**

   LVGL nécessite un fichier de configuration `lv_conf.h` :

   **Option A** : Copier depuis lib/
   ```bash
   # Le fichier lv_conf.h dans lib/ est déjà configuré
   cp sketches/JC3248W535C/lib/lv_conf.h ~/Arduino/libraries/lvgl/
   ```

   **Option B** : Utiliser le template
   ```bash
   cd ~/Arduino/libraries/lvgl/
   cp lv_conf_template.h lv_conf.h
   ```

   Puis éditer `lv_conf.h` et modifier :
   ```c
   // Ligne 15 environ
   #if 1  // Changer de 0 à 1 pour activer

   // Ligne 27 environ
   #define LV_COLOR_DEPTH 16

   // Ligne 91 environ
   #define LV_COLOR_16_SWAP 1  // Pour big endian
   ```

## Méthode 3 : Installation manuelle depuis GitHub

Pour les utilisateurs avancés qui veulent une version spécifique.

### Étapes

1. **Télécharger LVGL depuis GitHub**
   ```bash
   cd ~/Arduino/libraries/
   git clone --branch release/v8.3 https://github.com/lvgl/lvgl.git
   ```

2. **Copier le fichier de configuration**
   ```bash
   cp sketches/JC3248W535C/lib/lv_conf.h ~/Arduino/libraries/lvgl/
   ```

   Ou copier le template et le configurer comme dans la Méthode 2.

## Vérification de l'installation

### 1. Structure des dossiers

Après l'installation, tu dois avoir :
```
~/Arduino/libraries/lvgl/
├── lv_conf.h           ← IMPORTANT : doit être présent
├── lvgl.h
├── src/
│   ├── core/
│   ├── draw/
│   ├── extra/
│   ├── font/
│   ├── hal/
│   ├── misc/
│   └── widgets/
├── demos/
└── examples/
```

### 2. Test de compilation

Créer un sketch de test minimal :

```cpp
#include <lvgl.h>

void setup() {
    Serial.begin(115200);
    Serial.println("LVGL version: " + String(lv_version_major()) + "." +
                   String(lv_version_minor()) + "." +
                   String(lv_version_patch()));
}

void loop() {
    delay(1000);
}
```

**Compiler** (pas besoin d'uploader) :
- Si la compilation réussit → LVGL est correctement installé
- Si erreur `lvgl.h not found` → LVGL n'est pas dans le bon dossier
- Si erreur `lv_conf.h not found` → Copier lv_conf.h (voir ci-dessus)

## Configuration de lv_conf.h

Le fichier `lv_conf.h` contient toutes les options de configuration de LVGL.

### Paramètres critiques pour JC3248W535C

Ces paramètres sont **obligatoires** pour que l'afficheur fonctionne :

```c
/* Activer la configuration */
#if 1  // Ligne 15

/* Format couleur RGB565 */
#define LV_COLOR_DEPTH 16  // Ligne 27

/* Big endian pour RGB565 */
#define LV_COLOR_16_SWAP 1  // Ligne 91 (IMPORTANT)
```

### Paramètres optionnels utiles

```c
/* Moniteur de performance (affiche FPS) */
#define LV_USE_PERF_MONITOR 1  // Ligne ~688

/* Démos */
#define LV_USE_DEMO_WIDGETS 1  // Ligne ~755
#define LV_USE_DEMO_STRESS 1   // Ligne ~764

/* Support des images PNG */
#define LV_USE_PNG 1  // Ligne ~555

/* Support des images SJPEG */
#define LV_USE_SJPG 1  // Ligne ~568
```

## Dépannage

### Erreur : `lvgl.h: No such file or directory`

**Cause** : LVGL n'est pas installé ou pas dans le bon dossier.

**Solution** :
1. Vérifier que le dossier existe : `~/Arduino/libraries/lvgl/`
2. Vérifier que `lvgl.h` est à la racine de ce dossier
3. Redémarrer Arduino IDE

### Erreur : `lv_conf.h: No such file or directory`

**Cause** : Le fichier de configuration LVGL est manquant.

**Solution** :
```bash
cp sketches/JC3248W535C/lib/lv_conf.h ~/Arduino/libraries/lvgl/
```

### Erreur : `Please define or typedef lv_color_t` ou erreurs de couleurs

**Cause** : `lv_conf.h` n'est pas activé ou mal configuré.

**Solution** :
1. Ouvrir `~/Arduino/libraries/lvgl/lv_conf.h`
2. Ligne 15 : Changer `#if 0` en `#if 1`
3. Sauvegarder et recompiler

### Erreur : Couleurs inversées à l'écran

**Cause** : `LV_COLOR_16_SWAP` n'est pas activé.

**Solution** :
1. Ouvrir `~/Arduino/libraries/lvgl/lv_conf.h`
2. Chercher `LV_COLOR_16_SWAP`
3. Mettre à `1` : `#define LV_COLOR_16_SWAP 1`
4. Sauvegarder et recompiler/uploader

### Erreur : `Sketch too big`

**Cause** : LVGL prend beaucoup de mémoire Flash.

**Solution** :
1. Arduino IDE > Outils > Partition Scheme
2. Sélectionner "16M Flash (3MB APP/9.9MB FATFS)" ou similaire
3. Recompiler

### Warning : `#warning "IDF version less than 5.x"`

**Cause** : LVGL s'attend à ESP-IDF 5.x mais Core v3.0.2 utilise IDF 5.1.

**Solution** : Ce warning peut être ignoré. Le code fonctionne correctement.

## Version recommandée

Pour une compatibilité maximale avec cette carte :

**LVGL v8.3.x** (testé et fonctionnel)

Éviter LVGL v9.x qui a des changements d'API importants et peut nécessiter des modifications du code.

## Mise à jour de LVGL

Si tu veux mettre à jour LVGL :

1. **Sauvegarder lv_conf.h**
   ```bash
   cp ~/Arduino/libraries/lvgl/lv_conf.h ~/lv_conf.h.backup
   ```

2. **Supprimer l'ancienne version**
   ```bash
   rm -rf ~/Arduino/libraries/lvgl
   ```

3. **Installer la nouvelle version** (Méthode 1, 2 ou 3)

4. **Restaurer lv_conf.h**
   ```bash
   cp ~/lv_conf.h.backup ~/Arduino/libraries/lvgl/lv_conf.h
   ```

5. **Tester la compilation**

## Ressources LVGL

- [Documentation officielle](https://docs.lvgl.io/)
- [GitHub LVGL](https://github.com/lvgl/lvgl)
- [Forum LVGL](https://forum.lvgl.io/)
- [Exemples en ligne](https://docs.lvgl.io/master/examples.html)
- [Simulateur en ligne](https://sim.lvgl.io/)

## Support

En cas de problème avec LVGL :
1. Vérifier ce guide d'installation
2. Lire le [GUIDE_INITIALISATION_AFFICHEUR.md](./GUIDE_INITIALISATION_AFFICHEUR.md)
3. Consulter les logs de compilation pour identifier l'erreur exacte
4. Vérifier la version ESP32 Core (doit être v3.0.2)
