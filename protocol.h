/*
 * protocol.h - line-based USB-serial command parser.
 *
 * Reads characters non-blockingly into a buffer; on newline, tokenises the
 * line into a Command struct. Commands are case-insensitive on the verb.
 *
 * Grammar (args in [] optional):
 *   PING
 *   STATUS
 *   HOME
 *   FILL [ul]
 *   DISPENSE <ul>
 *   VALVE <DISPENSE|FILL|RINSE>
 *   READ
 *   CAL1 <pH>            (capture current measured volts as point 1)
 *   CAL2 <pH>            (capture current measured volts as point 2)
 *   CALV1 <pH> <V>       (set point 1 from a known voltage, no live read)
 *   CALV2 <pH> <V>       (set point 2 from a known voltage, no live read)
 *   CALSAVE
 *   TITRATE EP <targetPH> [dose_ul] [tol] [max_ul]
 *   TITRATE CURVE <step_ul> <max_ul> [settle_ms]
 *   STOP
 *   RESET
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <Arduino.h>
#include "config.h"
#include "valve.h"

enum CmdType {
  CMD_NONE, CMD_PING, CMD_STATUS, CMD_HOME, CMD_FILL, CMD_DISPENSE,
  CMD_VALVE, CMD_READ, CMD_CAL1, CMD_CAL2, CMD_CALV1, CMD_CALV2, CMD_CALSAVE,
  CMD_TITRATE_EP, CMD_TITRATE_CURVE, CMD_STOP, CMD_RESET, CMD_UNKNOWN
};

struct Command {
  CmdType type = CMD_NONE;
  float a = 0, b = 0, c = 0, d = 0;  // positional numeric args
  bool  hasA = false, hasB = false, hasC = false, hasD = false;
  ValvePos valve = VALVE_DISPENSE;
};

class CommandReader {
public:
  CommandReader();

  // Pump serial input. Returns true and fills `out` when a full line parsed.
  bool poll(Command& out);

private:
  char _buf[CMD_BUF_LEN];
  uint8_t _len;

  bool parse(char* line, Command& out);
};

#endif // PROTOCOL_H
