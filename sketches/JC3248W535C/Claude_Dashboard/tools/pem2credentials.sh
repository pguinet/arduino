#!/usr/bin/env bash
# Convertit les trois fichiers PEM du device "ecran" (Scaleway IoT Hub) en bloc C
# pour sketches/common/credentials.h :
#   <ca.pem>   -> DASH_MQTT_CA_CERT      (certificat CA du hub)
#   <cert.crt> -> DASH_MQTT_CLIENT_CERT  (certificat du device)
#   <cle.key>  -> DASH_MQTT_CLIENT_KEY   (cle privee du device, non chiffree)
#
# Usage : pem2credentials.sh <ca.pem> <cert.crt> <cle.key> [sortie.h]
#   Sans sortie.h, le bloc est ecrit sur la sortie standard.
#   Avec sortie.h, le fichier est cree en chmod 600 (il contient la cle privee).
#
# Les fins de ligne Windows (CRLF) et les lignes vides sont retirees au passage,
# sans modifier les fichiers d'origine. Si openssl est disponible, les fichiers
# sont verifies (PEM valides, cle non chiffree, cle correspondant au certificat).
set -euo pipefail

die() { echo "ERREUR : $*" >&2; exit 1; }

usage() {
  echo "Usage : $(basename "$0") <ca.pem> <cert.crt> <cle.key> [sortie.h]" >&2
  exit 2
}

[ $# -eq 3 ] || [ $# -eq 4 ] || usage
CA="$1"; CRT="$2"; KEY="$3"; OUT="${4:-}"

for f in "$CA" "$CRT" "$KEY"; do
  [ -r "$f" ] || die "fichier introuvable ou illisible : $f"
done

# Copies nettoyees (CRLF -> LF, lignes vides retirees) dans un dossier temporaire prive
umask 077
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
clean() { sed 's/\r$//; /^[[:space:]]*$/d' "$1" > "$2"; }
clean "$CA" "$TMP/ca.pem"
clean "$CRT" "$TMP/cert.crt"
clean "$KEY" "$TMP/cle.key"
ca="$TMP/ca.pem"; crt="$TMP/cert.crt"; key="$TMP/cle.key"

# Verifications (messages sur stderr : stdout peut porter le bloc)
if command -v openssl >/dev/null; then
  openssl x509 -in "$ca" -noout 2>/dev/null || die "$CA n'est pas un certificat PEM valide"
  openssl x509 -in "$crt" -noout 2>/dev/null || die "$CRT n'est pas un certificat PEM valide"
  if grep -q ENCRYPTED "$key"; then
    die "$KEY est chiffree : openssl pkey -in $KEY -out clair.key"
  fi
  openssl pkey -in "$key" -noout 2>/dev/null || die "$KEY n'est pas une cle privee PEM valide"
  pub_crt="$(openssl x509 -in "$crt" -noout -pubkey | openssl sha256)"
  pub_key="$(openssl pkey -in "$key" -pubout | openssl sha256)"
  [ "$pub_crt" = "$pub_key" ] || die "$KEY ne correspond pas a $CRT"
  {
    echo "Certificats OK :"
    echo "  CA     : $(openssl x509 -in "$ca" -noout -subject -enddate | tr '\n' ' ')"
    echo "  device : $(openssl x509 -in "$crt" -noout -subject -enddate | tr '\n' ' ')"
    echo "  cle    : non chiffree, correspond au certificat"
  } >&2
else
  echo "openssl absent : verifications sautees" >&2
fi

# Un #define par fichier, une chaine "...\n" par ligne, continuation "\" sauf a la fin
pem2c() {
  echo "#define $1 \\"
  sed 's/.*/    "&\\n" \\/' "$2" | sed '$ s/ \\$//'
}

block() {
  echo "// Genere par pem2credentials.sh le $(date '+%Y-%m-%d %H:%M')"
  pem2c DASH_MQTT_CA_CERT "$ca"
  pem2c DASH_MQTT_CLIENT_CERT "$crt"
  pem2c DASH_MQTT_CLIENT_KEY "$key"
}

if [ -z "$OUT" ]; then
  block
  exit 0
fi

# umask 077 : le fichier est cree en 600 ; chmod au cas ou il existait deja
block > "$OUT"
chmod 600 "$OUT"
{
  echo
  echo "Bloc genere : $OUT (chmod 600)"
  echo "1. Dans sketches/common/credentials.h, remplace les blocs DASH_MQTT_CA_CERT,"
  echo "   DASH_MQTT_CLIENT_CERT et DASH_MQTT_CLIENT_KEY par le contenu de ce fichier."
  echo "2. Puis supprime-le (il contient la cle privee) : shred -u \"$OUT\""
} >&2
