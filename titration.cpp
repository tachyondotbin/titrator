#include "titration.h"
#include "config.h"

// Below this |pH - target| the endpoint dose is shrunk for a finer approach.
#define EP_APPROACH_BAND   0.5f
#define EP_FINE_FACTOR     0.25f
#define EP_MIN_DOSE_UL     1.0f

// Stall guard: if pH moves less than this over EP_STALL_LIMIT consecutive
// doses, assume the titrant cannot reach the target and stop.
#define EP_STALL_DPH       0.01f
#define EP_STALL_LIMIT     8
#define EP_DEFAULT_MAX_UL  2000.0f

Titration::Titration(Pump& pump, Valve& valve, PHMeter& ph)
  : _pump(pump), _valve(valve), _ph(ph),
    _mode(M_EP), _phase(TP_IDLE),
    _doseUL(0), _settleMs(0), _t0(0), _settleDeadline(0),
    _targetPH(7), _tol(0.05f), _prevDiffSign(0), _havePrevDiff(false),
    _epLastPH(0), _haveEpLast(false), _stallCount(0),
    _maxUL(0), _prevPH(0), _prevVol(0), _havePrev(false),
    _maxSlope(0), _eqVol(0) {}

void Titration::startEndpoint(float targetPH, float doseUL, float tol, float maxUL) {
  _mode = M_EP;
  _targetPH = targetPH;
  _doseUL = (doseUL > 0) ? doseUL : 25.0f;
  _tol = (tol > 0) ? tol : 0.05f;
  _maxUL = (maxUL > 0) ? maxUL : EP_DEFAULT_MAX_UL;
  _settleMs = 1500;
  _havePrevDiff = false;
  _haveEpLast = false;
  _stallCount = 0;
  _pump.resetDispensed();
  _t0 = millis();
  _phase = TP_SET_VALVE;
  Serial.println(F("# titration EP start: t_ms,total_ul,pH,mV,phase"));
}

void Titration::startCurve(float stepUL, float maxUL, unsigned long settleMs) {
  _mode = M_CURVE;
  _doseUL = (stepUL > 0) ? stepUL : 50.0f;
  _maxUL = (maxUL > 0) ? maxUL : 1000.0f;
  _settleMs = (settleMs > 0) ? settleMs : 2000;
  _havePrev = false;
  _maxSlope = 0; _eqVol = 0;
  _pump.resetDispensed();
  _t0 = millis();
  _phase = TP_SET_VALVE;
  Serial.println(F("# titration CURVE start: t_ms,total_ul,pH,mV,phase"));
}

void Titration::stop() {
  if (active()) {
    _pump.stop();
    finish("STOPPED");
  }
}

void Titration::emit(float totalUL, float pH, float mV) {
  Serial.print(millis() - _t0); Serial.print(',');
  Serial.print(totalUL, 2);     Serial.print(',');
  Serial.print(pH, 3);          Serial.print(',');
  Serial.print(mV, 2);          Serial.print(',');
  Serial.println(_mode == M_EP ? "EP" : "CURVE");
}

float Titration::measure(float& mVout) {
  float v = _ph.readVolts(PH_AVG_SAMPLES);
  mVout = v * 1000.0f;
  return _ph.voltsToPH(v);
}

void Titration::finish(const char* reason) {
  _phase = TP_DONE;
  if (_mode == M_CURVE) {
    Serial.print(F("# CURVE done: "));
    Serial.print(reason);
    Serial.print(F("  equivalence_uL="));
    Serial.print(_eqVol, 2);
    Serial.print(F("  max_dpHdV="));
    Serial.println(_maxSlope, 4);
  } else {
    Serial.print(F("# EP done: "));
    Serial.println(reason);
  }
  Serial.println(F("OK titration complete"));
}

bool Titration::update() {
  switch (_phase) {
    case TP_IDLE:
    case TP_DONE:
      return false;

    case TP_SET_VALVE:
      _valve.moveTo(VALVE_DISPENSE);
      _settleDeadline = millis() + VALVE_MOVE_MS;
      _phase = TP_SETTLE;
      return true;

    case TP_SETTLE:
      if ((long)(millis() - _settleDeadline) >= 0) _phase = TP_MEASURE;
      return true;

    case TP_MEASURE: {
      float mV;
      float pH = measure(mV);
      float total = _pump.dispensedUL();
      emit(total, pH, mV);

      if (_mode == M_CURVE) {
        // dpH/dV vs previous point -> track equivalence (max |slope|).
        if (_havePrev) {
          float dV = total - _prevVol;
          if (dV > 1e-3f) {
            float slope = fabsf(pH - _prevPH) / dV;
            if (slope > _maxSlope) { _maxSlope = slope; _eqVol = total; }
          }
        }
        _prevPH = pH; _prevVol = total; _havePrev = true;

        if (total >= _maxUL) { finish("max volume reached"); return false; }
      } else { // M_EP
        float diff = pH - _targetPH;
        if (fabsf(diff) <= _tol) { finish("target reached"); return false; }
        float sign = (diff > 0) ? 1.0f : -1.0f;
        if (_havePrevDiff && sign != _prevDiffSign) {
          finish("target crossed"); return false;       // overshoot, cannot undo
        }
        _prevDiffSign = sign; _havePrevDiff = true;

        // Safety: bounded titrant volume.
        if (total >= _maxUL) { finish("max volume reached (target not met)"); return false; }

        // Safety: stall - pH not responding to repeated doses.
        if (_haveEpLast) {
          if (fabsf(pH - _epLastPH) < EP_STALL_DPH) {
            if (++_stallCount >= EP_STALL_LIMIT) {
              finish("stalled - pH not responding (titrant exhausted/wrong?)");
              return false;
            }
          } else {
            _stallCount = 0;
          }
        }
        _epLastPH = pH; _haveEpLast = true;
      }
      _phase = TP_DOSE;
      return true;
    }

    case TP_DOSE: {
      float dose = _doseUL;
      if (_mode == M_EP) {
        // shrink dose near the target for a finer endpoint
        // (re-measure cheap: use last emitted comparison via prevDiffSign band)
        // recompute current closeness from a quick single read
        float mV; float pH = measure(mV);
        if (fabsf(pH - _targetPH) < EP_APPROACH_BAND) {
          dose = _doseUL * EP_FINE_FACTOR;
          if (dose < EP_MIN_DOSE_UL) dose = EP_MIN_DOSE_UL;
        }
      }
      if (!_pump.startDispense(dose)) {
        finish("syringe empty - REFILL needed");
        return false;
      }
      _phase = TP_DOSE_WAIT;
      return true;
    }

    case TP_DOSE_WAIT:
      if (!_pump.isMoving()) {
        _settleDeadline = millis() + _settleMs;
        _phase = TP_SETTLE;   // settle, then measure again
      }
      return true;
  }
  return false;
}
