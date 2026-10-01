#include "tz_mapping.h"

#include <Arduino.h>
#include <string.h>

namespace harixos {
namespace api {
namespace tz {

// --- Helper: detect POSIX TZ strings ---

// IANA region names contain '/' (e.g. "Pacific/Auckland"). A POSIX TZ string
// can also contain '/' inside its DST rules ("M3.5.0/2"), but those always
// carry a ',' as well, so '/' without ',' identifies an IANA name.
bool isPosix(const char* s) {
  if (s == nullptr || *s == '\0') return false;
  if (strchr(s, '/') != nullptr && strchr(s, ',') == nullptr) return false;
  return true;
}

// --- Normalization to newlib-compatible POSIX TZ ---

namespace {

char g_posixBuf[96];

inline bool isAlphaC(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
inline bool isDigitC(char c) { return c >= '0' && c <= '9'; }

// True when an offset field (hh[:mm[:ss]], optionally signed) starts at p.
bool hasOffsetAt(const char* p) {
  if (isDigitC(*p)) return true;
  return (*p == '+' || *p == '-') && isDigitC(*(p + 1));
}

// Copies an offset field from p to *out. flipSign negates the sign so a bare
// ISO offset ("+12" = UTC+12) becomes the POSIX form ("-12").
const char* copyOffset(const char* p, char** out, char* end, bool flipSign) {
  char sign = 0;
  if (*p == '+' || *p == '-') {
    sign = *p;
    p++;
    if (flipSign) sign = (sign == '+') ? '-' : '+';
  } else if (flipSign) {
    sign = '-';
  }
  if (sign != 0 && *out < end) *(*out)++ = sign;
  while (isDigitC(*p) && *out < end) *(*out)++ = *p++;
  while (*p == ':') {
    if (*out < end) *(*out)++ = ':';
    p++;
    while (isDigitC(*p) && *out < end) *(*out)++ = *p++;
  }
  return p;
}

// newlib's tzset() only accepts a TZ string that starts with an alphabetic
// zone name, uses ',' between the two DST rules, and uses '/' only before a
// transition time. Table entries like "+12+1,M9.5.0/M4.1.0/3" or
// "-5|-5,M3.2.0,M11.1.0" violate those rules, so rewrite the string into a
// shape newlib can parse (otherwise the whole TZ value is silently ignored
// and the clock stays on UTC).
void normalizePosix(const char* s) {
  char* o = g_posixBuf;
  char* end = g_posixBuf + sizeof(g_posixBuf) - 1;
  const char* p = s;
  if (*p == ':') p++;  // POSIX allows a leading ':'

  bool named = isAlphaC(*p);
  if (named) {
    int n = 0;
    while (isAlphaC(*p) && n < 10 && o < end) { *o++ = *p++; n++; }
  } else {
    // Bare/ISO form has no zone name; newlib requires one.
    *o++ = 'U'; *o++ = 'T'; *o++ = 'C';
  }

  // Standard-time offset.
  if (hasOffsetAt(p)) {
    p = copyOffset(p, &o, end, !named);
  } else if (o < end) {
    *o++ = '0';
  }

  bool sawDstName = false;
  if (*p == '|') p++;  // table's std|dst separator
  if (isAlphaC(*p)) {  // DST zone name ("CEST")
    int n = 0;
    while (isAlphaC(*p) && n < 10 && o < end) { *o++ = *p++; n++; }
    sawDstName = true;
  }
  if (hasOffsetAt(p)) {
    if (named && sawDstName) {
      p = copyOffset(p, &o, end, false);  // real POSIX DST offset: keep as-is
    } else {
      // Bare "+12+1" (DST = std + 1h) or a redundant "|-5": newlib's
      // default DST offset (std - 1h in POSIX terms) already covers it.
      char scratch[16];
      char* t = scratch;
      p = copyOffset(p, &t, scratch + 15, false);
    }
  }

  if (*p == ',') {  // DST transition rules
    if (!sawDstName) { *o++ = 'D'; *o++ = 'S'; *o++ = 'T'; }
    for (; *p != '\0' && o < end; p++) {
      char c = *p;
      // '/' between rules is a glibc extension; newlib needs ',' there.
      if (c == '/' && isAlphaC(*(p + 1))) c = ',';
      *o++ = c;
    }
  }
  *o = '\0';
}

}  // namespace

// --- Mapping table ---

// Entries are organized by region. The table is sorted for binary search.
// Standard offset (non-DST) in minutes is stored.
// DST offset is encoded in the POSIX string (the |-N part).
static const TzEntry kTable[] = {
    // === UTC ===
    { "UTC", "+0", 0 },

    // === Americas ===
    { "America/New_York", "-5|-5,M3.2.0,M11.1.0", -300 },
    { "America/Chicago", "-6|-6,M3.2.0,M11.1.0", -360 },
    { "America/Denver", "-7|-7,M3.2.0,M11.1.0", -420 },
    { "America/Los_Angeles", "-8|-8,M3.2.0,M11.1.0", -480 },
    { "America/Sao_Paulo", "-3", -180 },
    { "America/Anchorage", "-9|-9,M3.2.0,M11.1.0", -540 },
    { "America/Halifax", "-4|-4,M3.2.0,M11.1.0", -240 },
    { "America/El_Salvador", "-6", -360 },
    { "America/Caracas", "-4:30", -270 },
    { "America/St_Johns", "-3:30|-3:30,M3.2.0,M11.1.0", -210 },
    { "America/Regina", "-6", -360 },
    { "America/Mexico_City", "-6|-6,M3.5.0/M10.5.0", -360 },
    { "America/Buenos_Aires", "-3", -180 },
    { "America/Lima", "-5", -300 },
    { "America/Bogota", "-5", -300 },
    { "America/Santiago", "-4|-4,M9.1.6/24,M4.1.6/24", -240 },

    // === Europe ===
    { "Europe/London", "GMT0BST,M3.5.0/M10.5.0", 0 },
    { "Europe/Paris", "CET-1CEST,M3.5.0/M10.5.0/3", 60 },
    { "Europe/Berlin", "CET-1CEST,M3.5.0/M10.5.0/3", 60 },
    { "Europe/Athens", "EET-2EEST,M3.5.0/3/M10.5.0/4", 120 },
    { "Europe/Moscow", "MSK-4", 180 },
    { "Europe/Dublin", "GMT0IST,M3.5.0/M10.5.0", 0 },
    { "Europe/Lisbon", "WET0WEST,M3.5.0/M10.5.0", 0 },
    { "Europe/Helsinki", "EET-2EEST,M3.5.0/3/M10.5.0/4", 120 },
    { "Europe/Warsaw", "CET-1CEST,M3.5.0/M10.5.0/3", 60 },
    { "Europe/Brussels", "CET-1CEST,M3.5.0/M10.5.0/3", 60 },
    { "Europe/Oslo", "CET-1CEST,M3.5.0/M10.5.0/3", 60 },
    { "Europe/Copenhagen", "CET-1CEST,M3.5.0/M10.5.0/3", 60 },
    { "Europe/Rome", "CET-1CEST,M3.5.0/M10.5.0/3", 60 },
    { "Europe/Stockholm", "CET-1CEST,M3.5.0/M10.5.0/3", 60 },
    { "Europe/Vienna", "CET-1CEST,M3.5.0/M10.5.0/3", 60 },
    { "Europe/Zurich", "CET-1CEST,M3.5.0/M10.5.0/3", 60 },
    { "Europe/Prague", "CET-1CEST,M3.5.0/M10.5.0/3", 60 },
    { "Europe/Budapest", "CET-1CEST,M3.5.0/M10.5.0/3", 60 },
    { "Europe/Bucharest", "EET-2EEST,M3.5.0/3/M10.5.0/4", 120 },
    { "Europe/Kiev", "EET-2EEST,M3.5.0/3/M10.5.0/4", 120 },
    { "Europe/Tallinn", "EET-2EEST,M3.5.0/3/M10.5.0/4", 120 },
    { "Europe/Riga", "EET-2EEST,M3.5.0/3/M10.5.0/4", 120 },
    { "Europe/Vilnius", "EET-2EEST,M3.5.0/3/M10.5.0/4", 120 },

    // === Africa ===
    { "Africa/Cairo", "EET-2EEST,M4.5.5/24,M10.5.5/24", 120 },
    { "Africa/Johannesburg", "SAST-2", 120 },
    { "Africa/Lagos", "WAT-1", 60 },
    { "Africa/Nairobi", "EAT-3", 180 },
    { "Africa/Casablanca", "WET0WEST,M3.5.0/M10.5.0", 0 },
    { "Africa/Algiers", "CET-1", 60 },
    { "Africa/Tripoli", "EET-2", 120 },
    { "Africa/Addis_Ababa", "EAT-3", 180 },
    { "Africa/Dar_es_Salaam", "EAT-3", 180 },
    { "Africa/Kampala", "EAT-3", 180 },
    { "Africa/Maputo", "CAT-2", 120 },
    { "Africa/Mogadishu", "EAT-3", 180 },
    { "Africa/Nouakchott", "GMT0", 0 },
    { "Africa/Accra", "GMT0", 0 },
    { "Africa/Dakar", "GMT0", 0 },

    // === Asia ===
    { "Asia/Dubai", "GST-4", 240 },
    { "Asia/Kolkata", "IST-5:30", 330 },
    { "Asia/Dhaka", "BST-6", 360 },
    { "Asia/Bangkok", "ICT-7", 420 },
    { "Asia/Shanghai", "CST-8", 480 },
    { "Asia/Tokyo", "JST-9", 540 },
    { "Asia/Seoul", "KST-9", 540 },
    { "Asia/Singapore", "SGT-8", 480 },
    { "Asia/Karachi", "PKT-5", 300 },
    { "Asia/Tashkent", "UZT-5", 300 },
    { "Asia/Colombo", "IST-5:30", 330 },
    { "Asia/Jerusalem", "IST-2IDT,M3.4.4/26,M10.5.5", 120 },
    { "Asia/Tehran", "IRST-3:30", 210 },
    { "Asia/Kabul", "AFT-4:30", 270 },
    { "Asia/Yakutsk", "YAKT-9", 540 },
    { "Asia/Vladivostok", "VLAT-10", 600 },
    { "Asia/Hong_Kong", "HKT-8", 480 },
    { "Asia/Taipei", "CST-8", 480 },
    { "Asia/Pyongyang", "KST-9", 540 },
    { "Asia/Manila", "PST-8", 480 },
    { "Asia/Jakarta", "WIB-7", 420 },
    { "Asia/Kuala_Lumpur", "SGT-8", 480 },
    { "Asia/Riyadh", "AST-3", 180 },
    { "Asia/Amman", "EET-2EEST,M3.5.5/0,M10.5.5/0", 120 },
    { "Asia/Beirut", "EET-2EEST,M3.5.5/0,M10.5.5/0", 120 },
    { "Asia/Damascus", "EET-2EEST,M3.5.5/0,M10.5.5/0", 120 },
    { "Asia/Baghdad", "AST-3", 180 },
    { "Asia/Aden", "AST-3", 180 },
    { "Asia/Muscat", "GST-4", 240 },
    { "Asia/Baku", "AZT-4", 240 },
    { "Asia/Yerevan", "AMT-4", 240 },
    { "Asia/Tbilisi", "GET-4", 240 },

    // === Oceania ===
    { "Australia/Sydney", "AEST-10AEDT,M10.1.0/M4.1.0/3", 600 },
    { "Australia/Melbourne", "AEST-10AEDT,M10.1.0/M4.1.0/3", 600 },
    { "Australia/Adelaide", "ACST-9:30ACDT,M10.1.0/M4.1.0/3", 570 },
    { "Australia/Perth", "AWST-8", 480 },
    { "Australia/Brisbane", "AEST-10", 600 },
    { "Pacific/Auckland", "+12+1,M9.5.0/M4.1.0/3", 720 },
    { "Pacific/Fiji", "+12+1,M1.1.0/M4.1.0/3", 720 },
    { "Pacific/Honolulu", "HST-10", -600 },
    { "Pacific/Tongatapu", "+13", 780 },
    { "Pacific/Guam", "ChST-10", 600 },
    { "Pacific/Port_Moresby", "PGT-10", 600 },
    { "Pacific/Noumea", "NCT-11", 660 },
    { "Pacific/Samoa", "SST-11", -660 },
    { "Pacific/Easter", "-6|-6,M9.1.6/22,M4.1.6/22", -360 },
};

static constexpr size_t kTableSize = sizeof(kTable) / sizeof(kTable[0]);

// --- Lookup ---

int findIndex(const char* ianaName) {
  if (ianaName == nullptr || *ianaName == '\0') return -1;
  for (size_t i = 0; i < kTableSize; ++i) {
    if (strcmp(kTable[i].iana, ianaName) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

const char* toPosix(const char* ianaName) {
  if (ianaName == nullptr || *ianaName == '\0') {
    return "UTC0";
  }
  if (isPosix(ianaName)) {
    normalizePosix(ianaName);
    return g_posixBuf;
  }
  int idx = findIndex(ianaName);
  if (idx >= 0) {
    normalizePosix(kTable[idx].posix);
    return g_posixBuf;
  }
  // Unknown IANA name: fall back to UTC.
  return "UTC0";
}

int32_t offsetMinutes(const char* ianaName) {
  if (ianaName == nullptr || *ianaName == '\0') {
    return 0;
  }
  if (!isPosix(ianaName)) {
    int idx = findIndex(ianaName);
    return idx >= 0 ? kTable[idx].offsetMinutes : 0;
  }
  // POSIX or bare offset string: parse the standard-time offset and return it
  // with the human/ISO sign convention (UTC+12 -> 720), matching the table.
  const char* p = ianaName;
  if (*p == ':') p++;
  bool named = isAlphaC(*p);
  if (named) {
    while (isAlphaC(*p)) p++;
  }
  int sign = 1;
  if (*p == '+') {
    p++;
  } else if (*p == '-') {
    sign = -1;
    p++;
  }
  int hours = 0;
  while (isDigitC(*p)) {
    hours = hours * 10 + (*p - '0');
    p++;
  }
  int minutes = 0;
  if (*p == ':') {
    p++;
    while (isDigitC(*p)) {
      minutes = minutes * 10 + (*p - '0');
      p++;
    }
  }
  int32_t posixMinutes = sign * (hours * 60 + minutes);
  // POSIX offsets are "value to add to local time to get UTC" (CET-1 is
  // UTC+1); flip to the ISO sign used by the mapping table.
  return named ? -posixMinutes : posixMinutes;
}

}  // namespace tz
}  // namespace api
}  // namespace harixos
