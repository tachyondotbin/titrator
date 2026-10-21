#include "protocol.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

CommandReader::CommandReader() : _len(0) { _buf[0] = 0; }

static void toUpper(char* s) {
  for (; *s; ++s) *s = toupper((unsigned char)*s);
}

bool CommandReader::poll(Command& out) {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (_len == 0) continue;          // ignore blank lines / CRLF pairs
      _buf[_len] = 0;
      bool ok = parse(_buf, out);
      _len = 0;
      if (ok) return true;
      // unparseable line still reported as UNKNOWN so caller can ERR
      out.type = CMD_UNKNOWN;
      return true;
    }
    if (_len < CMD_BUF_LEN - 1) _buf[_len++] = c;
  }
  return false;
}

bool CommandReader::parse(char* line, Command& out) {
  out = Command();

  // Tokenise (verb + up to 5 args).
  char* tok[6] = {0};
  int n = 0;
  char* p = strtok(line, " \t");
  while (p && n < 6) { tok[n++] = p; p = strtok(NULL, " \t"); }
  if (n == 0) return false;

  toUpper(tok[0]);

  if (!strcmp(tok[0], "PING"))    { out.type = CMD_PING;    return true; }
  if (!strcmp(tok[0], "STATUS"))  { out.type = CMD_STATUS;  return true; }
  if (!strcmp(tok[0], "HOME"))    { out.type = CMD_HOME;    return true; }
  if (!strcmp(tok[0], "READ"))    { out.type = CMD_READ;    return true; }
  if (!strcmp(tok[0], "CALSAVE")) { out.type = CMD_CALSAVE; return true; }
  if (!strcmp(tok[0], "STOP"))    { out.type = CMD_STOP;    return true; }
  if (!strcmp(tok[0], "RESET"))   { out.type = CMD_RESET;   return true; }

  if (!strcmp(tok[0], "FILL")) {
    out.type = CMD_FILL;
    if (n >= 2) { out.a = atof(tok[1]); out.hasA = true; }
    return true;
  }
  if (!strcmp(tok[0], "DISPENSE")) {
    if (n < 2) return false;
    out.type = CMD_DISPENSE; out.a = atof(tok[1]); out.hasA = true;
    return true;
  }
  if (!strcmp(tok[0], "CAL1") || !strcmp(tok[0], "CAL2")) {
    if (n < 2) return false;
    out.type = (tok[0][3] == '1') ? CMD_CAL1 : CMD_CAL2;
    out.a = atof(tok[1]); out.hasA = true;
    return true;
  }
  if (!strcmp(tok[0], "CALV1") || !strcmp(tok[0], "CALV2")) {
    if (n < 3) return false;                               // need pH and V
    out.type = (tok[0][4] == '1') ? CMD_CALV1 : CMD_CALV2;
    out.a = atof(tok[1]); out.hasA = true;                 // buffer pH
    out.b = atof(tok[2]); out.hasB = true;                 // known volts
    return true;
  }
  if (!strcmp(tok[0], "VALVE")) {
    if (n < 2) return false;
    toUpper(tok[1]);
    if      (!strcmp(tok[1], "DISPENSE")) out.valve = VALVE_DISPENSE;
    else if (!strcmp(tok[1], "FILL"))     out.valve = VALVE_FILL;
    else if (!strcmp(tok[1], "RINSE"))    out.valve = VALVE_RINSE;
    else return false;
    out.type = CMD_VALVE;
    return true;
  }
  if (!strcmp(tok[0], "TITRATE")) {
    if (n < 2) return false;
    toUpper(tok[1]);
    if (!strcmp(tok[1], "EP")) {
      if (n < 3) return false;
      out.type = CMD_TITRATE_EP;
      out.a = atof(tok[2]); out.hasA = true;            // target pH
      if (n >= 4) { out.b = atof(tok[3]); out.hasB = true; } // dose_ul
      if (n >= 5) { out.c = atof(tok[4]); out.hasC = true; } // tol
      if (n >= 6) { out.d = atof(tok[5]); out.hasD = true; } // max_ul
      return true;
    }
    if (!strcmp(tok[1], "CURVE")) {
      if (n < 4) return false;
      out.type = CMD_TITRATE_CURVE;
      out.a = atof(tok[2]); out.hasA = true;            // step_ul
      out.b = atof(tok[3]); out.hasB = true;            // max_ul
      if (n >= 5) { out.c = atof(tok[4]); out.hasC = true; } // settle_ms
      return true;
    }
    return false;
  }

  return false;  // unknown verb
}
