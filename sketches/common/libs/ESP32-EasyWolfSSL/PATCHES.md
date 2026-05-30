# ESP32-EasyWolfSSL — copie vendorisée et patchée

Copie locale de [`xorlent/ESP32-EasyWolfSSL`](https://github.com/Xorlent/ESP32-EasyWolfSSL)
v1.0.0, **patchée** pour pouvoir joindre des serveurs **TLS 1.3 exclusif derrière
une passerelle** (cas de l'API PRIM Île-de-France Mobilités depuis le 26/05/2026).

Vendorisée ici car les patches sont dans le code de la lib : les laisser dans
`.pio/libdeps/` les ferait écraser au prochain `pio pkg update` / clean.

## Patches appliqués à `src/WolfSSLClient.cpp`

1. **Méthode TLS flexible** (sinon verrou TLS 1.2)
   La lib d'origine force `wolfTLSv1_2_client_method()` dans `_createSSLContext()`,
   ce qui empêche toute négociation TLS 1.3.
   → remplacé par `wolfSSLv23_client_method()` (négocie la version la plus haute
   commune, donc TLS 1.3 si dispo).

2. **Envoi du SNI** (sinon `handshake_failure`, alert 40)
   La lib d'origine n'appelle jamais `wolfSSL_UseSNI()`. Or PRIM (derrière une
   passerelle) exige le SNI pour router le handshake.
   → ajout, dans `connect(host, port, timeout)` juste avant le handshake :
   ```c
   #ifdef HAVE_SNI
       if (host != nullptr) {
           wolfSSL_UseSNI(_ssl, WOLFSSL_SNI_HOST_NAME, host, (word16)strlen(host));
       }
   #endif
   ```

## Réglages requis côté projet (platformio.ini)

```ini
build_flags =
    -DWOLFSSL_USER_SETTINGS   ; wolfSSL utilise user_settings.h
    -DWOLFSSL_TLS13           ; user_settings.h livre TLS 1.3 commenté -> on l'active
    -DHAVE_SNI                ; active le support SNI dans wolfSSL (utilisé par le patch 2)
    -Wl,--allow-multiple-definition  ; Arduino-wolfSSL 5.7.2 : fonction non-inline dans wolfssl.h

lib_deps =
    wolfssl/Arduino-wolfSSL   ; le moteur TLS (non patché)
lib_extra_dirs =
    /home/pascal/github/arduino/sketches/common/libs  ; trouve cette copie patchée
lib_ignore = wolfssl          ; évite le paquet générique wolfssl/wolfssl (conflit ESPIDF/ARDUINO)
```

Côté sketch : remplacer `#include <WiFiClientSecure.h>` par `#include <WolfSSLClient.h>`
(qui fait `typedef WolfSSLClient WiFiClientSecure;`). Le reste du code ne change pas.

Validé en runtime le 30/05/2026 sur JC3248W535C : PRIM répond HTTP 200, TLS 1.3 confirmé.
