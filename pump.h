/*
 * pump.h - syringe pump on TMC2209 (STEP/DIR) via AccelStepper.
 *
 * Volume <-> microsteps conversion, non-blocking moves, homing to the EMPTY
 * limit switch, and hard interlocks at both ends. Limit switches are debounced
 * (reuses the debounce pattern from the original titrator.ino).
 *
 * Position convention: step position 0 = EMPTY (plunger fully expelled,
 * volume in syringe = 0). Increasing position FILLS the syringe.
 */
#ifndef PUMP_H
#define PUMP_H

#include <Arduino.h>
#include <AccelStepper.h>

class Pump {
public:
  Pump();

  void begin();

  // Call every loop iteration: services AccelStepper + debounces switches.
  // Returns true while a move is in progress.
  bool update();

  // Start homing: drive toward EMPTY switch slowly. Non-blocking; poll homed().
  void startHome();
  bool homing() const { return _homing; }
  bool homed()  const { return _homed; }

  // Start a dispense (push out) of volUL microlitres. Clamped so position
  // cannot go below EMPTY. Returns false if not enough volume loaded.
  bool startDispense(float volUL);

  // Start a fill (aspirate) of volUL microlitres. If volUL <= 0, fill to FULL
  // switch. Clamped so position cannot exceed MAX_STEPS / FULL switch.
  bool startFill(float volUL);

  // Abort any motion immediately.
  void stop();

  bool isMoving();

  // Volume currently held in the syringe (uL).
  float volumeUL();

  // Total volume dispensed since last resetDispensed() (uL) - for titration.
  float dispensedUL() const { return _dispensedUL; }
  void  resetDispensed() { _dispensedUL = 0; }

  // Debounced switch states (true = pressed/at end).
  bool atEmpty() const { return _emptyPressed; }
  bool atFull()  const { return _fullPressed; }

  // Set true when a move was cut short by a limit switch (interlock fired).
  bool faulted() const { return _fault; }
  void clearFault() { _fault = false; }

private:
  AccelStepper _stepper;

  bool _homing, _homed;
  bool _fault;
  float _dispensedUL;

  // Debounce state for the two limit switches.
  int  _lastRaw[2];
  int  _stable[2];
  unsigned long _lastEdge[2];
  bool _emptyPressed, _fullPressed;

  long _moveTargetBefore;  // position at start of current dispense (for accounting)

  void serviceSwitches();
  long ulToSteps(float ul) const;
  float stepsToUL(long steps) const;
};

#endif // PUMP_H
