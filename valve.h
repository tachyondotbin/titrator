/*
 * valve.h - 3-way fill valve on a servo.
 *
 * Three named positions: DISPENSE (syringe -> cell), FILL (syringe -> titrant
 * reservoir), RINSE (syringe -> rinse/waste). Moves are non-blocking from the
 * caller's view: position() reports when the servo has had time to settle.
 */
#ifndef VALVE_H
#define VALVE_H

#include <Arduino.h>
#include <Servo.h>

enum ValvePos { VALVE_DISPENSE, VALVE_FILL, VALVE_RINSE };

class Valve {
public:
  Valve();
  void begin();

  // Command a position. Records a settle deadline; non-blocking.
  void moveTo(ValvePos pos);

  // True once VALVE_MOVE_MS has elapsed since the last moveTo().
  bool settled() const;

  ValvePos position() const { return _pos; }
  const char* positionName() const;

private:
  Servo _servo;
  ValvePos _pos;
  unsigned long _settleAt;
};

#endif // VALVE_H
