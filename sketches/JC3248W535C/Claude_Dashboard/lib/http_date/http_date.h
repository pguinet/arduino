// Lecture de l'en-tete HTTP "Date:" (repli quand NTP est filtre), sans
// Arduino : testable en natif.
#pragma once

#include <time.h>

namespace httpdate {

// IMF-fixdate (RFC 9110), ex. "Wed, 08 Oct 2026 12:34:56 GMT", en temps Unix.
// Faux si le format ne correspond pas.
bool parse(const char *s, time_t *out);

// Ligne d'en-tete "Date: ..." (nom insensible a la casse, espaces tolores) :
// vrai et `out` rempli si c'est une date lisible.
bool parseHeaderLine(const char *line, time_t *out);

}  // namespace httpdate
