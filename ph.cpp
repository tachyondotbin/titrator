#include "ph.h"
#include "config.h"
#include <EEPROM.h>

PHMeter::PHMeter(Adc& adc, uint8_t channel)
  : _adc(adc), _ch(channel),
    _v1(0), _ph1(PH_CAL1_PH), _v2(0), _ph2(PH_CAL2_PH),
    _haveP1(false), _haveP2(false),
    _slopePHperV(1.0f / PH_FALLBACK_SLOPE), _offsetPH(PH_FALLBACK_OFFSET) {}

// EEPROM emulation on STM32duino stores bytes; pack floats via memcpy.
static void eeWriteU32(int addr, uint32_t v) {
  for (int i = 0; i < 4; i++) EEPROM.write(addr + i, (v >> (8 * i)) & 0xFF);
}
static uint32_t eeReadU32(int addr) {
  uint32_t v = 0;
  for (int i = 0; i < 4; i++) v |= ((uint32_t)EEPROM.read(addr + i)) << (8 * i);
  return v;
}
static void eeWriteFloat(int addr, float f) {
  uint32_t u; memcpy(&u, &f, 4); eeWriteU32(addr, u);
}
static float eeReadFloat(int addr) {
  uint32_t u = eeReadU32(addr); float f; memcpy(&f, &u, 4); return f;
}

void PHMeter::begin() {
  if (eeReadU32(EE_ADDR_MAGIC) == EE_MAGIC) {
    _slopePHperV = eeReadFloat(EE_ADDR_SLOPE);
    _offsetPH    = eeReadFloat(EE_ADDR_OFFSET);
    _haveP1 = _haveP2 = true; // model considered valid
  } else {
    // Theoretical fallback: slope is V/pH -> invert to pH/V.
    _slopePHperV = 1.0f / PH_FALLBACK_SLOPE;
    _offsetPH    = PH_FALLBACK_OFFSET;
  }
}

float PHMeter::readVolts(uint8_t samples) {
#if ADS_PH_DIFFERENTIAL
  _adc.setChannelDiff(_ch, ADS_PH_NEG_CHANNEL);
#else
  _adc.setChannel(_ch);
#endif
  if (samples < 1) samples = 1;
  double acc = 0;
  for (uint8_t i = 0; i < samples; i++) acc += _adc.readVolts();
  return (float)(acc / samples);
}

float PHMeter::voltsToPH(float v) const {
  return _offsetPH + _slopePHperV * v;
}

float PHMeter::readPH(uint8_t samples) {
  return voltsToPH(readVolts(samples));
}

void PHMeter::setCalPoint(uint8_t point, float bufferPH, float measuredVolts) {
  if (point == 1) { _v1 = measuredVolts; _ph1 = bufferPH; _haveP1 = true; }
  else            { _v2 = measuredVolts; _ph2 = bufferPH; _haveP2 = true; }
  recompute();
}

void PHMeter::recompute() {
  if (_haveP1 && _haveP2 && fabsf(_v2 - _v1) > 1e-6f) {
    _slopePHperV = (_ph2 - _ph1) / (_v2 - _v1);
    _offsetPH    = _ph1 - _slopePHperV * _v1;
  }
}

void PHMeter::save() {
  // STM32duino EEPROM emulation: each write() reads-modifies-writes the flash
  // page, so values persist without a separate commit() call.
  eeWriteFloat(EE_ADDR_SLOPE, _slopePHperV);
  eeWriteFloat(EE_ADDR_OFFSET, _offsetPH);
  eeWriteU32(EE_ADDR_MAGIC, EE_MAGIC);
}
