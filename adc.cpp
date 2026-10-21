#include "adc.h"
#include <ADS1256.h>   // CuriousScientist library (must be installed)
#include <SPI.h>

#define AINCOM 0x08

// Map PGA gain value -> ADS1256 PGA register bits [2:0] (also the library's
// PGA_* values). 2^bits = gain, which the library's convertToVoltage divides by.
static uint8_t pgaBits(uint8_t gain) {
  switch (gain) {
    case 1:  return 0;
    case 2:  return 1;
    case 4:  return 2;
    case 8:  return 3;
    case 16: return 4;
    case 32: return 5;
    case 64: return 6;
    default: return 0;
  }
}

Adc::Adc(int csPin, int drdyPin, int resetPin, int pwdnPin)
  : _lib(nullptr), _cs(csPin), _drdy(drdyPin), _reset(resetPin), _pwdn(pwdnPin),
    _pga(1), _vref(2.5f) {}

bool Adc::begin(uint8_t pgaGain, AdsDataRate rate, float vref) {
  _pga = pgaGain;
  _vref = vref;

  // Library ctor: (DRDY, RESET, SYNC/PDWN, CS, VREF, SPI*). SYNC unused here.
  int8_t resetPin = (_reset >= 0) ? (int8_t)_reset : ADS1256::PIN_UNUSED;
  _lib = new ADS1256((int8_t)_drdy, resetPin, ADS1256::PIN_UNUSED,
                     (int8_t)_cs, vref, &SPI);

  // Mirror the proven ph_debug bring-up sequence exactly:
  //   sd 0 1  ->  d 2.5  ->  g 1  ->  x (SELFCAL)  ->  measure
  _lib->InitializeADC();              // SPI.begin + reset + default regs + self-cal
  setChannelDiff(0, 1);               // sd 0 1  : differential AIN0-AIN1 (+SYNC)
  _lib->setDRATE((uint8_t)rate);      // d 2.5   : data rate
  _lib->setPGA(pgaBits(pgaGain));     // g 1     : gain
  _lib->sendDirectCommand(0xF0);      // x       : SELFCAL, last (after all config)

  // SELFCAL at 2.5 SPS takes a while; wait for it to finish (DRDY goes high
  // when busy, low when the calibrated conversion is ready) before readback.
  uint32_t t = millis();
  while (digitalRead(_drdy) == HIGH) if (millis() - t > 2000) break;
  delay(50);

  // DRATE readback as a comms / wiring sanity check (retry: SPI/cal may need
  // a moment to settle on first boot, so a single miss is not a real fault).
  for (int i = 0; i < 5; i++) {
    if (_lib->readRegister(0x03) == (long)(uint8_t)rate) return true;
    delay(50);
  }
  return false;
}

void Adc::setChannel(uint8_t ch) {
  setChannelDiff(ch, AINCOM);         // single-ended = AINx vs AINCOM
}

void Adc::setChannelDiff(uint8_t pos, uint8_t neg) {
  if (!_lib) return;
  // MUX: PSEL = pos (bits 7:4), NSEL = neg (bits 3:0).
  _lib->setMUX((uint8_t)(((pos & 0x0F) << 4) | (neg & 0x0F)));
  // After a MUX change the ADS1256 must be re-synced or the next conversion is
  // stale/unsettled (random ~full-scale samples). SYNC then WAKEUP restarts a
  // clean conversion on the new channel. (Datasheet p35; ph_debug reads only
  // after a settled stream.)
  _lib->sendDirectCommand(0xFC);      // SYNC
  delayMicroseconds(4);               // t11 (>= 24 tCLK)
  _lib->sendDirectCommand(0x00);      // WAKEUP -> resume conversions
}

void Adc::setDataRate(AdsDataRate rate) {
  if (!_lib) return;
  _lib->setDRATE((uint8_t)rate);
  _lib->sendDirectCommand(0xF0);      // self-cal at the new rate
  delay(200);
}

// Fast-MCU sync: the library's readSingle() does not wait for DRDY to return
// HIGH, so a 170 MHz MCU can loop back and latch a half-updated conversion
// (~0.5 V glitch). Block here until DRDY is high again before the next read.
void Adc::syncHighDRDY() {
#if F_CPU >= 48000000
  uint32_t t = millis();
  while (digitalRead(_drdy) == LOW)
    if (millis() - t > 1500) break;   // bounded so a stuck line can't hang us
#endif
}

int32_t Adc::readRaw(uint32_t /*timeoutMs*/) {
  if (!_lib) return 0;
  long raw = _lib->readSingle();
  syncHighDRDY();
  return (int32_t)raw;
}

float Adc::rawToVolts(int32_t raw) const {
  return _lib ? _lib->convertToVoltage(raw) : 0.0f;
}

float Adc::readVolts(uint32_t timeoutMs) {
  return rawToVolts(readRaw(timeoutMs));
}
