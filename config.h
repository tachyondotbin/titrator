/*
 * config.h - all pins, mechanical constants, calibration defaults, shared enums.
 *
 * Target: STM32G431CBU6
 *   Arduino IDE board: "Generic STM32G4 series" -> "STM32G431CBU6"
 *   USB support:       "CDC (generic 'Serial' supersede U(S)ART)"
 *   Upload method:     STM32CubeProgrammer (SWD) or DFU
 *
 * Edit the values marked "USER:" to match your hardware.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// =====================================================================
// Pump - TMC2209 in STEP/DIR mode (microsteps set by hardware MS1/MS2)
// =====================================================================
#define STEP_PIN            PA0     // TMC2209 STEP
#define DIR_PIN             PA1     // TMC2209 DIR
#define EN_PIN              PA8     // TMC2209 EN
// EN active level: standard TMC2209 = LOW enables the driver. If the motor is
// dead-silent / freewheeling on ALL commands, the driver may be disabled - try
// flipping this to HIGH (some carrier boards / wiring invert EN).
#define EN_ACTIVE_LEVEL     LOW     // level written to EN_PIN to ENABLE the driver

#define LIMIT_EMPTY_PIN     PA3     // limit switch: plunger fully expelled (HOME / zero)
#define LIMIT_FULL_PIN      PA4     // limit switch: plunger fully retracted (syringe full)

// Limit switches wired to GND, using internal pull-ups: pressed = LOW.
#define LIMIT_ACTIVE_LEVEL  LOW
#define DEBOUNCE_MS         20UL

// Motion direction: does increasing step position DISPENSE or FILL?
// If HOME / DISPENSE move the wrong way, flip this.
#define DIR_DISPENSE_SIGN   (+1)    // +1: positive steps push plunger out (dispense)

// Speeds (steps/sec) and accel (steps/sec^2). Tune on bench.
#define PUMP_MAX_SPEED      8000.0f
#define PUMP_ACCEL          4000.0f
#define PUMP_HOME_SPEED     1500.0f // slow approach to EMPTY switch
#define PUMP_MIN_PULSE_US   2       // TMC2209 STEP high >= ~100ns; 2us safe

// =====================================================================
// Servo - 3-way fill valve (SG90 or similar)
// =====================================================================
#define SERVO_PIN           PA2
#define VALVE_DISPENSE_DEG  90      // syringe port -> titration cell
#define VALVE_FILL_DEG      0       // syringe port -> titrant reservoir (aspirate)
#define VALVE_RINSE_DEG     180     // syringe port -> rinse / waste
#define VALVE_MOVE_MS       500     // settle time after a servo move

// =====================================================================
// ADS1256 - 24-bit ADC over SPI1
//   SPI1: SCK=PA5, MISO(DOUT)=PA6, MOSI(DIN)=PA7
// =====================================================================
#define ADS_CS_PIN          PB0
#define ADS_DRDY_PIN        PB1     // active LOW, data-ready
#define ADS_RESET_PIN       PB2     // optional hardware reset (-1 if unused)
#define ADS_PWDN_PIN        -1      // optional; tie high in hardware if unused

// Onboard reference (matches the proven ads1256_working.ino value).
// Affects absolute volts only; 2-point pH cal absorbs any residual error.
#define ADS_VREF            2.498f//2.648f // 2.496f //2.529f  // measured onboard ref (multimeter back-calc); edit to yours

// PGA: full-scale differential input = +/- (2*VREF/PGA).
//   With VREF=2.5V:  PGA=1 -> +/-5V,  PGA=2 -> +/-2.5V,  PGA=4 -> +/-1.25V.
// USER: pick to match your LMP91200 output swing.
#define ADS_PGA             1

// pH electrode input.
//   Single-ended (default): reads ADS_PH_CHANNEL (AINx) vs AINCOM.
//   Differential: set ADS_PH_DIFFERENTIAL to 1 to read
//                 ADS_PH_CHANNEL (+) - ADS_PH_NEG_CHANNEL (-).
//   Differential rejects common-mode noise; use it when the probe has a
//   separate reference lead (signal on +, reference on -).
#define ADS_PH_CHANNEL      0       // AIN0 (positive input)
#define ADS_PH_DIFFERENTIAL 1       // 0 = single-ended, 1 = differential (AIN0-AIN1)
#define ADS_PH_NEG_CHANNEL  1       // AIN1 (negative input, used only if diff)

// Data rate code written to DRATE register (see adc.h enum).
//   Lower = more averaging, lower noise, higher ENOB, slower.
//   2.5 SPS = max averaging / lowest noise / highest ENOB (slowest). Also sits
//   on the 50/60 Hz rejection notch. Best for a slow, stable pH electrode.
#define ADS_DATARATE        ADS_DR_2_5SPS

#define PH_AVG_SAMPLES      16      // samples averaged per pH read

// =====================================================================
// Syringe mechanics - used to compute microlitres per microstep.
// USER: set these to your real syringe + drive train.
// =====================================================================
#define SYRINGE_VOLUME_UL   1000.0f // total deliverable volume of syringe (uL)
#define SYRINGE_STROKE_MM   60.0f   // plunger travel for that full volume (mm)
#define LEADSCREW_PITCH_MM  8.0f    // linear advance per motor revolution (mm)
#define STEPS_PER_REV       200     // full steps/rev (1.8deg motor = 200)
#define MICROSTEPS          32      // TMC2209 hardware microstep setting

// uL per microstep (compile-time constant):
//   uL/mm   = SYRINGE_VOLUME_UL / SYRINGE_STROKE_MM
//   mm/ustep= LEADSCREW_PITCH_MM / (STEPS_PER_REV * MICROSTEPS)
#define UL_PER_STEP \
  ((SYRINGE_VOLUME_UL / SYRINGE_STROKE_MM) * \
   (LEADSCREW_PITCH_MM / ((float)STEPS_PER_REV * (float)MICROSTEPS)))

// Total usable travel in microsteps (EMPTY .. FULL).
#define MAX_STEPS \
  ((long)((SYRINGE_STROKE_MM / LEADSCREW_PITCH_MM) * \
          (float)STEPS_PER_REV * (float)MICROSTEPS))

// =====================================================================
// pH 2-point calibration (no temperature compensation).
//   Default buffers: 1.65 and 9.18. Recalibrate at runtime via CAL1/CAL2.
// =====================================================================
#define PH_CAL1_PH          1.65f
#define PH_CAL2_PH          9.18f
// Theoretical fallback slope if EEPROM not yet programmed (V per pH unit).
// Nernst at ~25C ~ -59.16 mV/pH. Sign depends on wiring.
#define PH_FALLBACK_SLOPE   (-0.05916f)
#define PH_FALLBACK_OFFSET  (7.0f)   // pH at 0 V (mid-scale electrode)

// EEPROM (emulated flash) layout
#define EE_MAGIC            0xA17C0001UL
#define EE_ADDR_MAGIC       0
#define EE_ADDR_SLOPE       4
#define EE_ADDR_OFFSET      8

// =====================================================================
// Serial
// =====================================================================
#define SERIAL_BAUD         115200
#define CMD_BUF_LEN         64

// =====================================================================
// Shared types
// =====================================================================
enum SysState {
  ST_IDLE,
  ST_HOMING,
  ST_READY,
  ST_FILLING,
  ST_DISPENSING,
  ST_TITRATE_EP,
  ST_TITRATE_CURVE,
  ST_MEASURING,
  ST_ERROR
};

const char* stateName(SysState s);

#endif // CONFIG_H
