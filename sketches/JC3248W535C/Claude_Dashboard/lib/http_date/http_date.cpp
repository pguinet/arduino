#include "http_date.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace httpdate {

namespace {

const char *const MONTHS[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// Jours depuis le 1970-01-01 (calendrier gregorien proleptique, H. Hinnant).
long long daysFromCivil(int y, int m, int d)
{
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (long long)era * 146097 + doe - 719468;
}

bool leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

}  // namespace

bool parse(const char *s, time_t *out)
{
    if (!s || !out) return false;
    char wday[4], mon[4], zone[4];
    int d, y, hh, mm, ss;
    if (sscanf(s, " %3[A-Za-z], %d %3s %d %d:%d:%d %3s", wday, &d, mon, &y, &hh, &mm, &ss, zone) != 8)
        return false;
    if (strcmp(zone, "GMT") != 0) return false;
    int m = 0;
    while (m < 12 && strcmp(mon, MONTHS[m]) != 0) m++;
    if (m == 12) return false;
    static const int DAYS_IN_MONTH[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int maxDay = DAYS_IN_MONTH[m] + (m == 1 && leap(y));
    if (y < 1970 || d < 1 || d > maxDay || hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 60)
        return false;
    long long t = daysFromCivil(y, m + 1, d) * 86400LL + hh * 3600 + mm * 60 + ss;
    if (sizeof(time_t) < 8 && t > 0x7fffffffLL) return false;
    *out = (time_t)t;
    return true;
}

bool parseHeaderLine(const char *line, time_t *out)
{
    if (!line) return false;
    while (*line == ' ' || *line == '\t') line++;
    static const char NAME[] = "date:";
    for (size_t i = 0; i < sizeof NAME - 1; i++)
        if (tolower((unsigned char)line[i]) != NAME[i]) return false;
    return parse(line + sizeof NAME - 1, out);
}

}  // namespace httpdate
