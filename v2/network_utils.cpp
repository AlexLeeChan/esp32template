/* ==============================================================================
   NETWORK_UTILS.CPP - Network Utility Functions Implementation
   
   Implements network helper functions for:
   - Converting IP addresses to/from strings
   - Validating network configurations
   - Formatting network information for display
   
   Shared utilities to avoid code duplication.
   ============================================================================== */

#include "network_utils.h"

String cleanString(const String& input) {
  if (input.length() == 0) return String();
  const char* str = input.c_str();
  int len = input.length();
  int start = 0, end = len - 1;

  while (start < len && isspace((unsigned char)str[start])) start++;
  while (end >= start && isspace((unsigned char)str[end])) end--;

  return (start > end) ? String() : input.substring(start, end + 1);
}

bool isValidIP(const String& s) {
  IPAddress ip;
  if (!ip.fromString(s)) return false;
  return !(ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0);
}

bool isValidSubnet(const String& s) {
  IPAddress sub;
  if (!sub.fromString(s)) return false;
  uint32_t m = ~(uint32_t)sub;
  return (m & (m + 1)) == 0;
}

IPAddress parseIP(const String& s) {
  IPAddress ip;
  if (ip.fromString(s)) return ip;

  int p[4] = { 0 };
  int idx = 0, st = 0;
  for (int i = 0; i <= (int)s.length() && idx < 4; i++) {
    if (i == (int)s.length() || s[i] == '.') {
      p[idx++] = s.substring(st, i).toInt();
      st = i + 1;
    }
  }
  return IPAddress(p[0], p[1], p[2], p[3]);
}
