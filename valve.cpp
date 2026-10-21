#include "valve.h"
#include "config.h"

Valve::Valve() : _pos(VALVE_DISPENSE), _settleAt(0) {}

void Valve::begin() {
  _servo.attach(SERVO_PIN);
  moveTo(VALVE_DISPENSE);
}

void Valve::moveTo(ValvePos pos) {
  int deg;
  switch (pos) {
    case VALVE_FILL:  deg = VALVE_FILL_DEG;  break;
    case VALVE_RINSE: deg = VALVE_RINSE_DEG; break;
    case VALVE_DISPENSE:
    default:          deg = VALVE_DISPENSE_DEG; break;
  }
  _servo.write(deg);
  _pos = pos;
  _settleAt = millis() + VALVE_MOVE_MS;
}

bool Valve::settled() const {
  return (long)(millis() - _settleAt) >= 0;
}

const char* Valve::positionName() const {
  switch (_pos) {
    case VALVE_FILL:  return "FILL";
    case VALVE_RINSE: return "RINSE";
    case VALVE_DISPENSE:
    default:          return "DISPENSE";
  }
}
