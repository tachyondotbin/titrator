/*
 * ph.h - pH measurement: 2-point linear calibration, averaged reads,
 *        EEPROM persistence. No temperature compensation (fixed temp).
 *
 *   pH(V) = offset + slope_pH_per_V * V
 * where the two-point fit comes from CAL1/CAL2 (buffer pH vs measured volts).
 */
#ifndef PH_H
#define PH_H

#include <Arduino.h>
#include "adc.h"

class PHMeter {
public:
  explicit PHMeter(Adc& adc, uint8_t channel);

  // Load calibration from EEPROM; fall back to theoretical Nernst if unset.
  void begin();

  // Averaged input voltage at the pH channel (volts).
  float readVolts(uint8_t samples);

  // Averaged pH using current calibration.
  float readPH(uint8_t samples);

  // Convert a known voltage to pH with current calibration.
  float voltsToPH(float v) const;

  // Capture a calibration point: store (bufferPH, current measured volts).
  // point = 1 or 2. Recomputes slope/offset once both points exist.
  void setCalPoint(uint8_t point, float bufferPH, float measuredVolts);

  // Persist current slope/offset to EEPROM.
  void save();

  // Accessors for STATUS / debug.
  float slope()  const { return _slopePHperV; }
  float offset() const { return _offsetPH; }
  bool  calibrated() const { return _haveP1 && _haveP2; }

private:
  Adc&     _adc;
  uint8_t  _ch;

  // Stored cal points (volts, pH).
  float _v1, _ph1, _v2, _ph2;
  bool  _haveP1, _haveP2;

  // Active linear model: pH = offset + slope * V.
  float _slopePHperV;
  float _offsetPH;

  void recompute();
};

#endif // PH_H
