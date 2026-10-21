/*
 * titration.h - the two titration routines as a non-blocking state machine.
 *
 *  Endpoint (EP):   dose titrant in increments until the measured pH reaches a
 *                   target (within tolerance) or crosses it. Dose size shrinks
 *                   on approach for a finer endpoint.
 *  Curve (CURVE):   dose fixed increments up to a max volume, measuring pH after
 *                   each step. Streams the full curve and reports the equivalence
 *                   point as the volume of maximum |dpH/dV|.
 *
 * Both stream CSV rows:  t_ms,total_ul,pH,mV,phase
 * update() must be called every loop; pump.update() must also run each loop so
 * the dispense moves actually execute.
 */
#ifndef TITRATION_H
#define TITRATION_H

#include <Arduino.h>
#include "pump.h"
#include "valve.h"
#include "ph.h"

class Titration {
public:
  Titration(Pump& pump, Valve& valve, PHMeter& ph);

  // EP: dose toward targetPH. maxUL bounds total titrant (safety for extreme
  // targets / wrong-strength titrant); <=0 uses a default. Stalls out if pH
  // stops responding to repeated doses.
  void startEndpoint(float targetPH, float doseUL, float tol, float maxUL);
  void startCurve(float stepUL, float maxUL, unsigned long settleMs);

  // Advance the routine. Returns true while a titration is active.
  bool update();

  void stop();
  bool active() const { return _phase != TP_IDLE && _phase != TP_DONE; }

private:
  enum Mode  { M_EP, M_CURVE };
  enum Phase { TP_IDLE, TP_SET_VALVE, TP_SETTLE, TP_MEASURE,
               TP_EVAL, TP_DOSE, TP_DOSE_WAIT, TP_DONE };

  Pump&    _pump;
  Valve&   _valve;
  PHMeter& _ph;

  Mode  _mode;
  Phase _phase;

  // shared params
  float _doseUL;          // EP increment / CURVE step
  unsigned long _settleMs;
  unsigned long _t0;
  unsigned long _settleDeadline;

  // EP params
  float _targetPH, _tol;
  float _prevDiffSign;
  bool  _havePrevDiff;
  float _epLastPH;          // pH at previous EP measurement (stall detection)
  bool  _haveEpLast;
  uint8_t _stallCount;      // consecutive doses with negligible pH change

  // CURVE params + equivalence tracking
  float _maxUL;
  float _prevPH, _prevVol;
  bool  _havePrev;
  float _maxSlope, _eqVol;

  void emit(float totalUL, float pH, float mV);
  float measure(float& mVout);
  void finish(const char* reason);
};

#endif // TITRATION_H
