/*
 * titrator.ino - syringe-pump titrator with pH measurement.
 *
 * Target : STM32G431CBU6 (STM32duino), USB CDC serial.
 * Modules: config.h  ads1256.*  ph.*  pump.*  valve.*  protocol.*  titration.*
 *
 * Hardware chain:
 *   pH electrode -> LMP91200 (pH AFE) -> ADS1256 AIN0 -> SPI1 -> MCU
 *   TMC2209 (STEP/DIR) drives the syringe lead screw; 2 limit switches = ends.
 *   Servo drives a 3-way fill valve (titrant / dispense / rinse).
 *
 * Control: line-based commands over USB serial (see protocol.h).
 *
 * Board settings in Arduino IDE:
 *   Board   : Generic STM32G4 series -> STM32G431CBU6
 *   USB      : CDC (generic 'Serial' supersede U(S)ART)
 *   Upload   : STM32CubeProgrammer (SWD) or DFU
 */
#include "config.h"
#include "adc.h"
#include "ph.h"
#include "pump.h"
#include "valve.h"
#include "protocol.h"
#include "titration.h"

// --- module instances ---
Adc           adc(ADS_CS_PIN, ADS_DRDY_PIN, ADS_RESET_PIN, ADS_PWDN_PIN);
PHMeter       ph(adc, ADS_PH_CHANNEL);
Pump          pump;
Valve         valve;
Titration     titration(pump, valve, ph);
CommandReader reader;

SysState gState = ST_IDLE;
bool     gAdcOk = false;

// Deferred move: set the valve, wait for it to settle, then start the pump.
enum PendingOp { OP_NONE, OP_FILL, OP_DISPENSE };
PendingOp pendingOp = OP_NONE;
float     pendingVol = 0;

// Status LED: solid = ready, fast blink = busy, slow blink = fault (ADC not responding)
static void ledUpdate() {
  static uint32_t last = 0;
  static bool level = false;
  uint32_t now = millis();

  bool busy = (gState == ST_HOMING || gState == ST_FILLING ||
               gState == ST_DISPENSING || gState == ST_TITRATE_EP ||
               gState == ST_TITRATE_CURVE || gState == ST_MEASURING);

  if (!gAdcOk || gState == ST_ERROR) {
    if (now - last >= 500) { last = now; level = !level; digitalWrite(STATUS_LED_PIN, level); }
  } else if (busy) {
    if (now - last >= 100) { last = now; level = !level; digitalWrite(STATUS_LED_PIN, level); }
  } else {
    digitalWrite(STATUS_LED_PIN, HIGH);
  }
}

const char* stateName(SysState s) {
  switch (s) {
    case ST_IDLE:          return "IDLE";
    case ST_HOMING:        return "HOMING";
    case ST_READY:         return "READY";
    case ST_FILLING:       return "FILLING";
    case ST_DISPENSING:    return "DISPENSING";
    case ST_TITRATE_EP:    return "TITRATE_EP";
    case ST_TITRATE_CURVE: return "TITRATE_CURVE";
    case ST_MEASURING:     return "MEASURING";
    case ST_ERROR:         return "ERROR";
    default:               return "?";
  }
}

static void printStatus() {
  Serial.print(F("STATE="));   Serial.print(stateName(gState));
  Serial.print(F(" vol_uL=")); Serial.print(pump.volumeUL(), 1);
  Serial.print(F(" valve=")); Serial.print(valve.positionName());
  Serial.print(F(" empty=")); Serial.print(pump.atEmpty());
  Serial.print(F(" full="));  Serial.print(pump.atFull());
  Serial.print(F(" adc="));   Serial.print(gAdcOk ? "OK" : "FAIL");
  Serial.print(F(" ph_slope_pH_per_V=")); Serial.print(ph.slope(), 4);
  Serial.print(F(" ph_offset="));         Serial.println(ph.offset(), 4);
}

static void doRead() {
  float v = ph.readVolts(PH_AVG_SAMPLES);
  Serial.print(F("OK pH=")); Serial.print(ph.voltsToPH(v), 3);
  Serial.print(F(" mV="));   Serial.print(v * 1000.0f, 2);
  Serial.print(F(" V="));    Serial.println(v, 6);
}

static void dispatch(const Command& c) {
  // While titrating, only STOP / STATUS / READ are honoured.
  if (titration.active() &&
      c.type != CMD_STOP && c.type != CMD_STATUS && c.type != CMD_READ) {
    Serial.println(F("ERR busy: titration running (use STOP)"));
    return;
  }

  switch (c.type) {
    case CMD_PING:
      Serial.println(F("PONG"));
      break;

    case CMD_STATUS:
      printStatus();
      break;

    case CMD_HOME:
      pump.startHome();
      gState = ST_HOMING;
      Serial.println(F("OK homing"));
      break;

    case CMD_FILL:
      valve.moveTo(VALVE_FILL);
      pendingOp = OP_FILL;
      pendingVol = c.hasA ? c.a : -1.0f;   // <=0 => fill to FULL switch
      gState = ST_FILLING;
      Serial.println(F("OK filling"));
      break;

    case CMD_DISPENSE:
      valve.moveTo(VALVE_DISPENSE);
      pendingOp = OP_DISPENSE;
      pendingVol = c.a;
      gState = ST_DISPENSING;
      Serial.println(F("OK dispensing"));
      break;

    case CMD_VALVE:
      valve.moveTo(c.valve);
      Serial.print(F("OK valve=")); Serial.println(valve.positionName());
      break;

    case CMD_READ:
      doRead();
      break;

    case CMD_CAL1:
    case CMD_CAL2: {
      float v = ph.readVolts(PH_AVG_SAMPLES);
      ph.setCalPoint(c.type == CMD_CAL1 ? 1 : 2, c.a, v);
      Serial.print(F("OK cal")); Serial.print(c.type == CMD_CAL1 ? '1' : '2');
      Serial.print(F(" pH=")); Serial.print(c.a, 3);
      Serial.print(F(" V="));  Serial.println(v, 6);
      break;
    }

    case CMD_CALV1:
    case CMD_CALV2: {
      ph.setCalPoint(c.type == CMD_CALV1 ? 1 : 2, c.a, c.b);  // pH, known V
      Serial.print(F("OK cal")); Serial.print(c.type == CMD_CALV1 ? '1' : '2');
      Serial.print(F(" pH=")); Serial.print(c.a, 3);
      Serial.print(F(" V="));  Serial.print(c.b, 6);
      Serial.print(F(" slope_pH_per_V=")); Serial.print(ph.slope(), 4);
      Serial.print(F(" offset="));          Serial.println(ph.offset(), 4);
      break;
    }

    case CMD_CALSAVE:
      ph.save();
      Serial.println(F("OK cal saved"));
      break;

    case CMD_TITRATE_EP:
      titration.startEndpoint(c.a, c.hasB ? c.b : 0, c.hasC ? c.c : 0,
                              c.hasD ? c.d : 0);
      gState = ST_TITRATE_EP;
      Serial.println(F("OK titrate EP"));
      break;

    case CMD_TITRATE_CURVE:
      titration.startCurve(c.a, c.b, c.hasC ? (unsigned long)c.c : 0);
      gState = ST_TITRATE_CURVE;
      Serial.println(F("OK titrate CURVE"));
      break;

    case CMD_STOP:
      titration.stop();
      pump.stop();
      pendingOp = OP_NONE;
      gState = ST_READY;
      Serial.println(F("OK stopped"));
      break;

    case CMD_RESET:
      titration.stop();
      pump.stop();
      pendingOp = OP_NONE;
      gState = ST_IDLE;
      Serial.println(F("OK reset (run HOME before dosing)"));
      break;

    case CMD_UNKNOWN:
    default:
      Serial.println(F("ERR unknown command"));
      break;
  }
}


void setup() {
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);   // off until init is done
  Serial.begin(SERIAL_BAUD);

  uint32_t t0 = millis();
  while (!Serial && (millis() - t0 < 3000)) {
    ;
  }

  Serial.println("BOOT: setup started");

  Serial.println("BOOT: pump.begin");
  pump.begin();
  Serial.println("BOOT: pump OK");

  Serial.println("BOOT: valve.begin");
  valve.begin();
  Serial.println("BOOT: valve OK");

  Serial.println("BOOT: adc.begin");
  gAdcOk = adc.begin(ADS_PGA, ADS_DATARATE, ADS_VREF);
  Serial.print("BOOT: adc result=");
  Serial.println(gAdcOk);

  Serial.println("BOOT: ph.begin");
  ph.begin();
  Serial.println("BOOT: ph OK");

  Serial.println("BOOT: READY");
}

void loop() {
  // LED
  ledUpdate();
  // 1. Service motion + titration every pass (non-blocking).
  
  pump.update();
  if (titration.active()) titration.update();

  // 2. Start a deferred pump move once the valve has settled.
  if (pendingOp != OP_NONE && valve.settled()) {
    bool ok = (pendingOp == OP_FILL) ? pump.startFill(pendingVol)
                                     : pump.startDispense(pendingVol);
    if (!ok) {
      Serial.println(F("ERR move rejected (volume/limit)"));
      gState = ST_READY;
    }
    pendingOp = OP_NONE;
  }

  // 3. Detect completion of async operations -> back to READY.
  if (gState == ST_HOMING && pump.homed()) {
    gState = ST_READY;
    Serial.println(F("OK homed (vol=0)"));
  } else if ((gState == ST_FILLING || gState == ST_DISPENSING) &&
             pendingOp == OP_NONE && !pump.isMoving()) {
    if (pump.faulted()) { Serial.println(F("# limit switch reached")); pump.clearFault(); }
    Serial.print(F("OK done vol_uL=")); Serial.println(pump.volumeUL(), 1);
    gState = ST_READY;
  } else if ((gState == ST_TITRATE_EP || gState == ST_TITRATE_CURVE) &&
             !titration.active()) {
    gState = ST_READY;
  }

  // 4. Handle one incoming command per pass.
  Command cmd;
  if (reader.poll(cmd)) dispatch(cmd);
}
