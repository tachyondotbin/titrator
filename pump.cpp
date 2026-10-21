#include "pump.h"
#include "config.h"

Pump::Pump()
  : _stepper(AccelStepper::DRIVER, STEP_PIN, DIR_PIN),
    _homing(false), _homed(false), _fault(false), _dispensedUL(0),
    _emptyPressed(false), _fullPressed(false), _moveTargetBefore(0) {
  _lastRaw[0] = _lastRaw[1] = HIGH;
  _stable[0]  = _stable[1]  = HIGH;
  _lastEdge[0] = _lastEdge[1] = 0;
}

void Pump::begin() {
  pinMode(EN_PIN, OUTPUT);
  digitalWrite(EN_PIN, EN_ACTIVE_LEVEL);   // enable driver (see config.h)
  pinMode(LIMIT_EMPTY_PIN, INPUT_PULLUP);
  pinMode(LIMIT_FULL_PIN,  INPUT_PULLUP);

  _stepper.setMinPulseWidth(PUMP_MIN_PULSE_US);
  _stepper.setMaxSpeed(PUMP_MAX_SPEED);
  _stepper.setAcceleration(PUMP_ACCEL);
  // Flip DIR if the mechanism moves the wrong way (DIR_DISPENSE_SIGN = -1).
  _stepper.setPinsInverted(DIR_DISPENSE_SIGN < 0, false, false);
  _stepper.setCurrentPosition(0);
}

long Pump::ulToSteps(float ul) const {
  return (long)lroundf(ul / UL_PER_STEP);
}
float Pump::stepsToUL(long steps) const {
  return (float)steps * UL_PER_STEP;
}

void Pump::serviceSwitches() {
  const int pins[2] = { LIMIT_EMPTY_PIN, LIMIT_FULL_PIN };
  for (int i = 0; i < 2; i++) {
    int raw = digitalRead(pins[i]);
    if (raw != _lastRaw[i]) _lastEdge[i] = millis();
    if (millis() - _lastEdge[i] > DEBOUNCE_MS) _stable[i] = raw;
    _lastRaw[i] = raw;
  }
  _emptyPressed = (_stable[0] == LIMIT_ACTIVE_LEVEL);
  _fullPressed  = (_stable[1] == LIMIT_ACTIVE_LEVEL);
}

bool Pump::update() {
  serviceSwitches();

  long prevPos = _stepper.currentPosition();

  if (_homing) {
    if (_emptyPressed) {
      _stepper.setSpeed(0);
      _stepper.stop();
      _stepper.setCurrentPosition(0);
      _homing = false;
      _homed = true;
      return false;
    }
    _stepper.runSpeed();          // constant slow speed toward EMPTY
    return true;
  }

  // Interlocks: block motion into a pressed end-stop.
  long toGo = _stepper.distanceToGo();
  if (toGo < 0 && _emptyPressed) {        // moving toward EMPTY (dispense)
    _stepper.moveTo(_stepper.currentPosition());
    _fault = true;
  } else if (toGo > 0 && _fullPressed) {  // moving toward FULL (fill)
    _stepper.moveTo(_stepper.currentPosition());
    _fault = true;
  }

  _stepper.run();

  // Account dispensed volume from any decrease in position.
  long delta = prevPos - _stepper.currentPosition();
  if (delta > 0) _dispensedUL += stepsToUL(delta);

  return _stepper.distanceToGo() != 0;
}

void Pump::startHome() {
  _homing = true;
  _homed = false;
  _fault = false;
  _stepper.setSpeed(-PUMP_HOME_SPEED);   // negative = toward position 0 = EMPTY
}

bool Pump::startDispense(float volUL) {
  if (_homing) return false;
  if (volUL <= 0) return false;
  long steps = ulToSteps(volUL);
  long avail = _stepper.currentPosition();   // steps above EMPTY = volume held
  if (steps > avail) return false;           // not enough loaded
  _fault = false;
  _stepper.moveTo(_stepper.currentPosition() - steps);
  return true;
}

bool Pump::startFill(float volUL) {
  if (_homing) return false;
  _fault = false;
  long target;
  if (volUL <= 0) {
    target = MAX_STEPS;                      // fill until FULL switch trips
  } else {
    target = _stepper.currentPosition() + ulToSteps(volUL);
    if (target > MAX_STEPS) target = MAX_STEPS;
  }
  _stepper.moveTo(target);
  return true;
}

void Pump::stop() {
  _homing = false;
  _stepper.setSpeed(0);
  _stepper.moveTo(_stepper.currentPosition());
}

bool Pump::isMoving() {
  return _homing || _stepper.distanceToGo() != 0;
}

float Pump::volumeUL() {
  return stepsToUL(_stepper.currentPosition());
}
