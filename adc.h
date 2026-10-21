/*
 * adc.h - thin adapter over the CuriousScientist ADS1256 library.
 * (File is named adc.* - NOT ads1256.* - to avoid a case-insensitive clash
 *  with the library's <ADS1256.h> on Windows/macOS filesystems.)
 *
 * The proven library (installed as <ADS1256.h>) handles SPI, DRDY timing,
 * register access and the code->volts conversion. This adapter exposes the
 * small API the titrator firmware already uses (begin / setChannel /
 * setChannelDiff / setDataRate / readRaw / readVolts) and adds a fast-MCU
 * DRDY-high guard after each single read (the library's readSingle() omits it,
 * which on a 170 MHz G431 causes occasional ~0.5 V glitches).
 *
 * REQUIRES the library to be installed:
 *   Arduino IDE -> Sketch -> Include Library -> Manage Libraries -> "ADS1256"
 *   (Curious Scientist), or copy ADS1256.h/.cpp into Arduino/libraries/ADS1256.
 *
 * SPI: library uses SPI_MODE1, MSB first, ~1.92 MHz on the default SPI bus
 * (STM32 SPI1 = SCK PA5, MISO PA6, MOSI PA7).
 */
#ifndef ADS1256_ADAPTER_H
#define ADS1256_ADAPTER_H

#include <Arduino.h>

// Data-rate codes == ADS1256 DRATE register values (identical to the library's
// DRATE_* macros). Lower = more averaging, lower noise, higher ENOB, slower.
enum AdsDataRate {
  ADS_DR_30000SPS = 0xF0,
  ADS_DR_15000SPS = 0xE0,
  ADS_DR_7500SPS  = 0xD0,
  ADS_DR_3750SPS  = 0xC0,
  ADS_DR_2000SPS  = 0xB0,
  ADS_DR_1000SPS  = 0xA1,
  ADS_DR_500SPS   = 0x92,
  ADS_DR_100SPS   = 0x82,
  ADS_DR_60SPS    = 0x72,
  ADS_DR_50SPS    = 0x63,
  ADS_DR_30SPS    = 0x53,
  ADS_DR_25SPS    = 0x43,
  ADS_DR_10SPS    = 0x23,
  ADS_DR_5SPS     = 0x13,
  ADS_DR_2_5SPS   = 0x03
};

class ADS1256;  // CuriousScientist library class (full def pulled in by .cpp)

class Adc {
public:
  Adc(int csPin, int drdyPin, int resetPin = -1, int pwdnPin = -1);

  // Construct + init the library instance, set PGA / data rate / channel.
  // pgaGain in {1,2,4,8,16,32,64}. Returns false if the DRATE register does
  // not read back what we wrote (no device / wiring fault).
  bool begin(uint8_t pgaGain, AdsDataRate rate, float vref);

  // Single-ended channel (AINp = ch, AINn = AINCOM) for next reads.
  void setChannel(uint8_t ch);

  // Differential pair: AINp = pos, AINn = neg (0..7; 8 = AINCOM).
  void setChannelDiff(uint8_t pos, uint8_t neg);

  // Change output data rate at runtime (re-runs self-cal).
  void setDataRate(AdsDataRate rate);

  // Blocking single conversion -> signed 24-bit code (sign-extended).
  // timeoutMs kept for API compatibility; the library blocks on DRDY itself.
  int32_t readRaw(uint32_t timeoutMs = 1200);

  // Convert a raw code to input volts (uses the library's PGA/VREF model).
  float rawToVolts(int32_t raw) const;

  // Read one sample and return volts.
  float readVolts(uint32_t timeoutMs = 1200);

  uint8_t pga() const { return _pga; }

private:
  ADS1256* _lib;
  int _cs, _drdy, _reset, _pwdn;
  uint8_t _pga;
  float _vref;

  void syncHighDRDY();
};

#endif // ADS1256_ADAPTER_H
