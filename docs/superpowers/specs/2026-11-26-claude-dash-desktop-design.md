# Claude Dashboard — version poste de travail Linux (Tk sur X déporté)

## Intention

Le tableau de bord Claude Code tourne aujourd'hui sur un écran physique
(JC3248W535C, `sketches/JC3248W535C/Claude_Dashboard/`). Au bureau, l'utilisateur
ne veut plus avoir à transporter cet écran : il veut le même tableau de bord dans
une fenêtre de son poste.

Première version : une appli graphique Linux qui tourne sur le serveur
IF-VL-SEC-P1-PRI (sans environnement graphique) et s'affiche par X11 déporté
(SSH `-X`) sur le serveur X de MobaXterm, sous Windows. D'autres versions poste
de travail (Windows, macOS natifs) pourront suivre ; elles ne font pas partie de
ce lot.

## Objectifs

- Montrer les mêmes informations que l'écran physique : tous les serveurs qui
  publient sur le hub, quotas 5h/7j, cartes de session triées par urgence,
  bandeau « serveur injoignable », oubli d'un serveur muet depuis 1 h, retrait
  immédiat sur arrêt propre d'un agent.
- Alerter comme l'écran, avec la cloche X à la place des bips.
- Rester affichée en permanence au premier plan dans un coin de l'écran.
- Préparer les futures versions Windows/macOS : du Python + Tk qui tourne tel
  quel hors Linux (pas de dépendance à Xlib dans le code de l'appli).

## Non-objectifs

- Versions Windows/macOS natives, paquet installable sur le poste.
- Veille de l'écran (sans objet sur un poste).
- Modifier le firmware ou le format du snapshot publié par l'agent.
- Lancement automatique par systemd (le `DISPLAY` change à chaque session SSH).

## Anti-objectifs

- Une fenêtre lourde à afficher par le réseau X (rendu en images plein cadre,
  rafraîchissements continus) : elle reste ouverte toute la journée depuis le
  bureau.
- Perturber l'écran physique : s'il reste allumé à la maison, les deux doivent
  fonctionner en même temps.
- Une logique qui diverge en silence de celle de l'écran (tri, péremption,
  choix des quotas, déclenchement des alertes).

## Contraintes

- Hôte : IF-VL-SEC-P1-PRI, Debian 13, Python 3.13, `python3-tk` (Tk 8.6) déjà
  installé, aucun bureau. Fuseau `Europe/Paris`. Aucun paquet système à ajouter.
- Le 8883 sortant est filtré sur cet hôte : l'agent y publie déjà en MQTT sur
  WebSocket (`transport = "websockets"`, port 443). L'appli doit pouvoir faire
  de même.
- Scaleway IoT Hub, plan Shared : mTLS obligatoire, client id = Device ID, un
  Device ID ne supporte qu'une connexion à la fois, pas de retained, QoS 0/1.
  L'état d'un serveur arrive au plus tard à son prochain heartbeat (20 s).
- Serveur X : MobaXterm (`vendor string: Moba/X`, écran 3840×1930). Essai fait
  le 2026-11-26 avec une fenêtre Tk :
  - `_NET_WM_STATE_ABOVE` est honoré (fenêtre toujours devant), mais **ne peut
    pas être retiré** une fois posé ;
  - la cloche X (`XBell`, `Tk.bell()`) est entendue sur le poste ;
  - l'indicateur « urgent » de `WM_HINTS` ne produit rien de visible ;
    `_NET_WM_STATE_DEMANDS_ATTENTION` n'est pas annoncé dans `_NET_SUPPORTED`.
- CI (`.github/workflows/ci.yml`) : ruff, mypy `strict`, pytest sous Python 3.11
  et 3.13, sans affichage. Les tests ne doivent pas avoir besoin d'un serveur X.
- Code, commentaires, commits en anglais ; README du dashboard en français
  (comme l'existant).

## Approches envisagées

| Approche | Pour | Contre |
|---|---|---|
| **A. Python + Tkinter, dans le paquet `claude_dash` de l'agent** (retenue) | Tk déjà présent ; config TOML, paho-mqtt et mTLS/wss déjà écrits dans l'agent ; Tk dessine en primitives X (léger en déporté) ; même code utilisable plus tard sous Windows/macOS | La logique de `lib/dash_model/` est à porter en Python (~300 lignes) et à garder alignée avec le C++ ; rendu proche de l'écran mais pas identique au pixel |
| B. Interface LVGL de l'écran compilée pour PC (SDL) | Rendu identique ; `dash_model` C++ réutilisé tel quel | Client MQTT TLS en C et chaîne de build native à ajouter ; fenêtre 480×320 fixe ; SDL pousse des images entières par le réseau X ; portage Windows plus lourd |
| C. GTK3 (PyGObject) | Rendu plus moderne | Rendu côté client poussé en images par X : lent en déporté ; pénible à distribuer sous Windows |

A a été choisie par l'utilisateur : la plus légère en X déporté et la seule qui
prépare les versions Windows/macOS.

## Ce qui est décidé

### Données

- L'appli s'abonne en QoS 0 à `claude-dash/+/state` (même topic que l'écran,
  `MQTT_TOPIC` dans `src/main.cpp`) et lit le snapshot JSON publié par l'agent
  (format produit par `server/claude_dash/snapshot.py`).
- Message vide sur un topic : arrêt propre de l'agent, le serveur est retiré.
- **Device Scaleway dédié `poste`** : publish `reject #`, subscribe
  `accept claude-dash/#`. Il permet à l'appli et à l'écran d'être connectés en
  même temps. Sa création dans la console reste une étape manuelle de
  l'utilisateur, à documenter dans le README.

### Logique

Port Python du comportement de `lib/dash_model/` (voir `dash_model.h`), dans un
module pur, sans Tk ni MQTT, testé par pytest :

- parsing tolérant d'un snapshot (JSON invalide ou sans `host` : ignoré ;
  sessions non-objets ou sans `id` : ignorées) ;
- agrégation multi-serveurs, retrait sur message vide, oubli après
  `FORGET_AFTER_S` = 3600 s sans nouvelles ;
- tri : permission > question > attente > travaille, puis `since` croissant
  (`since` inconnu en tête de son groupe) ;
- quotas fenêtre par fenêtre depuis le serveur aux quotas les plus frais
  (`limits.updated`, sinon `ts`), en ignorant une fenêtre dont le reset est
  passé ;
- péremption : âge max des serveurs > `STALE_AFTER_S` = 180 s → bandeau et
  liste estompée ;
- alerte sur changement d'état d'une session déjà connue, la plus prioritaire
  l'emporte (permission > question > attente) ; pas d'alerte au premier snapshot
  d'un serveur ni au retour d'un serveur périmé ;
- helpers : durée (`0:42`, `5:10`, `1h05`, `2j`, `999j+`), couleur de jauge
  (vert < 60 %, orange < 85 %, rose au-delà), libellés d'état.

Les tests Python reprennent les cas des tests natifs de l'écran
(`test/test_dashboard`, `test/test_parse`, `test/test_helpers`) pour que les
deux implémentations restent alignées.

### Fenêtre

- Format de l'écran (paysage, ~480×320 au démarrage), **redimensionnable** :
  la liste de cartes s'allonge quand on agrandit en hauteur, les jauges
  s'élargissent avec la largeur.
- Même contenu et mêmes couleurs que l'écran (voir le README du dashboard,
  « Ce que montre l'écran ») : en-tête (nom du serveur s'il n'y en a qu'un,
  pastille MQTT, heure locale), quotas 5h/7j avec reset, cartes (projet ou
  `projet@hôte` si plusieurs serveurs, modèle, état, outil, durée dans l'état,
  jauge de contexte), bandeau rose « Serveur injoignable depuis N min ».
- Tant qu'aucun serveur n'a publié : état de la connexion MQTT (connexion en
  cours, connecté en attente du premier heartbeat, erreur avec sa raison), à la
  place de l'état WiFi/heure/MQTT de l'écran. « Aucune session active » ensuite.
- **Toujours au premier plan** (`-topmost`), sans option pour le retirer :
  MobaXterm ne le retire pas.

### Alertes

- Cloche X : deux coups pour permission et question, un coup pour attente
  (la cloche n'a qu'un son, le nombre de coups remplace les tonalités de
  l'écran).
- Repère visuel : la carte concernée clignote quelques secondes dans la couleur
  de son état. Pas d'indicateur « urgent » (sans effet sous MobaXterm, et
  inutile pour une fenêtre toujours devant).

### Lancement et installation

- Nouvelle commande `claude-dash-desktop` dans `server/pyproject.toml`,
  installée par `server/install.sh` dans le venv existant comme les autres.
- Lancée à la main depuis **un onglet SSH MobaXterm dédié**, laissé ouvert
  (décision de l'utilisateur) ; elle se ferme avec cet onglet. Pas de
  lancement par la commande de démarrage de session : chaque onglet lancerait
  sa propre instance.
- **Une seule instance** (le device `poste` n'admet qu'une connexion) : un
  second lancement affiche que l'appli tourne déjà (avec son PID) et s'arrête
  sans se connecter (verrou fichier, par exemple `fcntl.flock` sur un fichier
  sous `$XDG_RUNTIME_DIR`). Le README indique quoi faire si une autre machine
  utilise le même device (déconnexions en boucle).
- Configuration séparée de celle de l'agent, `~/.config/claude-dash/desktop.toml`
  (chmod 600, mêmes règles que `config.toml`) : section `[mqtt]` avec les mêmes
  clés que l'agent (hôte, port, `transport`, CA, certificat et clé du device
  `poste`, `client_id`, `topic_prefix`). Un `desktop.toml.example` versionné.

### Arbitrages faits pendant la relecture (à valider)

- Le modèle Python renvoie, en plus de l'alerte, la ou les sessions dont le
  changement d'état l'a déclenchée, pour savoir quelle carte faire clignoter.
- Comme le firmware (`src/main.cpp`), un message dont le `host` du payload ne
  correspond pas au topic est ignoré.
- Position de départ de la fenêtre : un coin par défaut, réglable dans
  `desktop.toml`.
- Tailles de police choisies pour rester lisibles sur l'écran 3840×1930 du
  poste.

## Laissé au constructeur

- Organisation des modules dans `claude_dash/` (modèle, client MQTT, UI) et
  partage du code de config avec l'agent.
- Passage des messages du thread réseau paho vers la boucle Tk (file + `after`).
- Polices, tailles et mise en page exacte, du moment que la fenêtre reste
  lisible à ~480×320 et légère en X déporté (pas de redessin complet à chaque
  seconde : seules l'heure et les durées changent sans message).
- Plafonds : l'écran limite à 4 serveurs et 12 sessions par serveur pour sa
  mémoire ; l'appli n'est pas tenue à ces plafonds.
- Comportement en cas de config absente ou invalide (message clair puis sortie,
  comme l'agent).
