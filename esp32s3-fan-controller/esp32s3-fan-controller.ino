// ESP32-S3 Fan Controller — Blynk Edgent (UICPAL ESP32-S3-CAM N16R8)
// WiFi credentials and auth token are provisioned at runtime via the Blynk app.
// No hardcoded passwords or tokens.
//
// Virtual pin layout:
//   --- Fan Controller ---
//   V0  Fan ON/OFF        (button 0/1)
//   V1  Fan speed         (slider 32-255)
//   V2  Timer ON/OFF      (button 0/1)
//   V3  Timer length      (slider, minutes)
//   V4  Timer countdown   (read-only display, minutes remaining)
//   V5  Firmware version  (read-only String)
//   V6  Uptime            (read-only String, updated every 5 min)
//
//   --- Stepper Motor ---
//   V10 Rotation ON/OFF   (button 0/1)
//   V11 Rotation speed    (slider, steps/s)
//   V12 Max angle         (slider, steps)
//   V13 Current angle     (read-only display, steps)
//   V14 Reset angle       (button, momentary)
//   V15 Angle update      (toggle 0/1 — enable/disable V13 angle reporting)
//
// Hardware pins:
//   GPIO15  PWM output  → fan speed (LEDC ch 4, 25 kHz)
//   GPIO4   Stepper IN0
//   GPIO5   Stepper IN1
//   GPIO6   Stepper IN2
//   GPIO7   Stepper IN3
//   GPIO21  Watchdog heartbeat TX → Arduino Nano guardian (9600 baud)
//   GPIO48  WS2812 status LED
//   GPIO0   BOOT / factory-reset button (hold 5 s)

#define BLYNK_FIRMWARE_VERSION  "0.1.0"
#define BLYNK_PRINT             Serial
#define APP_DEBUG

#define USE_ESP32S3_DEV_MODULE

#define BLYNK_TEMPLATE_ID "TMPL6PyORnvfe"
#define BLYNK_TEMPLATE_NAME "Home Fan Controller"

#include "Settings.h"
#include "BlynkEdgent.h"

// ------------------------------------------------------------------ //
//  Hardware pin definitions                                           //
// ------------------------------------------------------------------ //
#define PWM_PIN       15
#define SM_PIN0        4
#define SM_PIN1        5
#define SM_PIN2        6
#define SM_PIN3        7

// LEDC channel reserved for fan PWM (channels 1-3 used by Indicator.h RGB LED)
#define FAN_LEDC_CHANNEL   4
#define FAN_LEDC_FREQ      25000   // 25 kHz — above audible range
#define FAN_LEDC_BITS      8       // 8-bit duty: 0-255

// ------------------------------------------------------------------ //
//  Stepper motor constants                                             //
// ------------------------------------------------------------------ //
#define SM_MIN_INTERVAL_MS   3     // hard floor ~333 steps/s
#define SM_MAX_SPEED         300   // steps/s (matches the ESP8266 version)

// ------------------------------------------------------------------ //
//  Virtual pin aliases                                                 //
// ------------------------------------------------------------------ //
#define VPIN_FAN_ON_OFF       V0
#define VPIN_FAN_SPEED        V1
#define VPIN_TIMER_ON_OFF     V2
#define VPIN_TIMER_LENGTH     V3
#define VPIN_TIMER_COUNTDOWN  V4
#define VPIN_VERSION          V5
#define VPIN_UPTIME           V6

#define VPIN_SM_ON_OFF        V10
#define VPIN_SM_SPEED         V11
#define VPIN_SM_MAX_ANGLE     V12
#define VPIN_SM_CURRENT_ANGLE V13
#define VPIN_SM_RESET_ANGLE   V14
#define VPIN_SM_ANGLE_UPDATE  V15   // toggle: 1 = push V13, 0 = suppress

uint32_t boot_ms;
uint8_t  g_logLevel = LOG_LEVEL_OFF;

// ------------------------------------------------------------------ //
//  Forward declarations                                               //
// ------------------------------------------------------------------ //
void sm_step_func(uint16_t step_idx);
void update_step_cb(void *p_sm);
void update_timer_cb(void *p_fc);

// ------------------------------------------------------------------ //
//  PWM helpers (ESP32 core 3.x API)                                   //
// ------------------------------------------------------------------ //
static void fan_pwm_init() {
  ledcAttach(PWM_PIN, FAN_LEDC_FREQ, FAN_LEDC_BITS);
}

static void fan_pwm_write(uint8_t duty) {
  ledcWrite(PWM_PIN, duty);
}

// ------------------------------------------------------------------ //
//  StepperMotor                                                        //
// ------------------------------------------------------------------ //
class StepperMotor {
public:
  StepperMotor();

  void init();
  void run();
  void vpin_handler(uint8_t pin, BlynkParam const *param);
  void step();
  void stop() { _unset_timer(true); }

private:
  void _switch_on_handler(BlynkParam const *param);
  void _speed_handler(BlynkParam const *param);
  void _max_angle_handler(BlynkParam const *param);
  void _current_angle_handler(BlynkParam const *param);
  void _reset_angle_handler(BlynkParam const *param);

  void _set_timer();
  void _unset_timer(bool update_blynk = false);

  uint8_t  _blk_speed;
  uint16_t _blk_max_angle;
  int16_t  _blk_current_angle;
  bool     _direction;            // true = clockwise
  bool     _angle_update_enabled; // V15: whether to push V13
  uint32_t _step_ts_ms;
  int      _timer_id;

  BlynkTimer _timer;
};

StepperMotor::StepperMotor()
  : _blk_speed(0), _blk_max_angle(0), _blk_current_angle(0)
  , _direction(true), _angle_update_enabled(true), _step_ts_ms(0), _timer_id(-1) {}

void StepperMotor::init() {
  _unset_timer(true);
}

void StepperMotor::run() {
  if (_timer_id >= 0)
    _timer.run();
}

void StepperMotor::vpin_handler(uint8_t pin, BlynkParam const *param) {
  switch (pin) {
    case 10: _switch_on_handler(param);   break;
    case 11: _speed_handler(param);       break;
    case 12: _max_angle_handler(param);   break;
    case 13: _current_angle_handler(param); break;
    case 14: _reset_angle_handler(param); break;
    case 15: _angle_update_enabled = (param->asInt() != 0); break;
  }
}

void StepperMotor::step() {
  if (_blk_current_angle >= (int16_t)_blk_max_angle) {
    _blk_current_angle = _blk_max_angle;
    _direction = false;
  } else if (_blk_current_angle <= 0) {
    _blk_current_angle = 0;
    _direction = true;
  }

  if (_direction) _blk_current_angle++;
  else            _blk_current_angle--;

  sm_step_func((uint16_t)_blk_current_angle);

  uint32_t now = millis();
  uint32_t elapse = now - _step_ts_ms;
  if (elapse >= 1000) {
    if (_angle_update_enabled)
      Blynk.virtualWrite(VPIN_SM_CURRENT_ANGLE, _blk_current_angle);
    _step_ts_ms = now;
  }
}

void StepperMotor::_switch_on_handler(BlynkParam const *param) {
  int value = param->asInt();
  if (value == 0) _unset_timer();
  else            _set_timer();
  LOG_INFO(String("SM on/off: ") + value);
}

void StepperMotor::_speed_handler(BlynkParam const *param) {
  _blk_speed = (uint8_t)param->asInt();
  // Restart the timer at the new interval if currently running
  if (_timer_id >= 0) {
    _unset_timer();
    _set_timer();
  }
  LOG_INFO(String("SM speed: ") + _blk_speed);
}

void StepperMotor::_max_angle_handler(BlynkParam const *param) {
  _blk_max_angle = (uint16_t)param->asInt();
}

void StepperMotor::_current_angle_handler(BlynkParam const *param) {
  _blk_current_angle = (int16_t)param->asInt();
}

void StepperMotor::_reset_angle_handler(BlynkParam const *param) {
  if (param->asInt() == 1) {
    _blk_current_angle = 0;
    _direction = true;
    Blynk.virtualWrite(VPIN_SM_CURRENT_ANGLE, 0);
    Blynk.virtualWrite(VPIN_SM_RESET_ANGLE, 0);
  }
}

void StepperMotor::_set_timer() {
  if (_timer_id >= 0) _unset_timer();
  if (_blk_speed == 0) return;

  int interval = 1000 / _blk_speed;
  if (interval < SM_MIN_INTERVAL_MS) interval = SM_MIN_INTERVAL_MS;

  _step_ts_ms = millis();
  _timer_id = _timer.setInterval(interval, update_step_cb, this);
  Blynk.virtualWrite(VPIN_SM_ON_OFF, 1);
}

void StepperMotor::_unset_timer(bool update_blynk) {
  if (_timer_id >= 0) {
    _timer.deleteTimer(_timer_id);
    _timer_id = -1;
  }
  digitalWrite(SM_PIN0, LOW);
  digitalWrite(SM_PIN1, LOW);
  digitalWrite(SM_PIN2, LOW);
  digitalWrite(SM_PIN3, LOW);

  Blynk.virtualWrite(VPIN_SM_CURRENT_ANGLE, _blk_current_angle);
  if (update_blynk)
    Blynk.virtualWrite(VPIN_SM_ON_OFF, 0);
}

// Half-step sequence (8 steps, identical to the ESP8266 version)
void sm_step_func(uint16_t step_idx) {
  uint8_t phase = step_idx % 8;
  switch (phase) {
    case 0: digitalWrite(SM_PIN0,LOW);  digitalWrite(SM_PIN1,LOW);  digitalWrite(SM_PIN2,LOW);  digitalWrite(SM_PIN3,HIGH); break;
    case 1: digitalWrite(SM_PIN0,LOW);  digitalWrite(SM_PIN1,LOW);  digitalWrite(SM_PIN2,HIGH); digitalWrite(SM_PIN3,HIGH); break;
    case 2: digitalWrite(SM_PIN0,LOW);  digitalWrite(SM_PIN1,LOW);  digitalWrite(SM_PIN2,HIGH); digitalWrite(SM_PIN3,LOW);  break;
    case 3: digitalWrite(SM_PIN0,LOW);  digitalWrite(SM_PIN1,HIGH); digitalWrite(SM_PIN2,HIGH); digitalWrite(SM_PIN3,LOW);  break;
    case 4: digitalWrite(SM_PIN0,LOW);  digitalWrite(SM_PIN1,HIGH); digitalWrite(SM_PIN2,LOW);  digitalWrite(SM_PIN3,LOW);  break;
    case 5: digitalWrite(SM_PIN0,HIGH); digitalWrite(SM_PIN1,HIGH); digitalWrite(SM_PIN2,LOW);  digitalWrite(SM_PIN3,LOW);  break;
    case 6: digitalWrite(SM_PIN0,HIGH); digitalWrite(SM_PIN1,LOW);  digitalWrite(SM_PIN2,LOW);  digitalWrite(SM_PIN3,LOW);  break;
    case 7: digitalWrite(SM_PIN0,HIGH); digitalWrite(SM_PIN1,LOW);  digitalWrite(SM_PIN2,LOW);  digitalWrite(SM_PIN3,HIGH); break;
  }
}

void update_step_cb(void *p_sm) {
  ((StepperMotor *)p_sm)->step();
}

// ------------------------------------------------------------------ //
//  FanController                                                       //
// ------------------------------------------------------------------ //
class FanController {
public:
  FanController(StepperMotor *sm);

  void init();
  void run();
  void vpin_handler(uint8_t pin, BlynkParam const *param);
  void update_timer();

private:
  void _start_fan(bool update_blynk = false);
  void _stop_fan(bool update_blynk = false);
  void _set_timer();
  void _unset_timer(bool update_blynk = false);
  void _apply_speed();

  static const uint8_t MIN_SPEED = 32;
  static const uint8_t MAX_SPEED = 255;

  StepperMotor *_sm;
  BlynkTimer    _timer;
  int           _timer_id;

  int      _blk_switch_on;
  int      _blk_speed;
  int      _blk_timer_on;
  int      _blk_timer_time;
  int      _remaining_time;
  uint32_t _timer_start_ts_ms;
};

FanController::FanController(StepperMotor *sm)
  : _sm(sm), _timer_id(-1)
  , _blk_switch_on(0), _blk_speed(MIN_SPEED)
  , _blk_timer_on(0), _blk_timer_time(0), _remaining_time(0)
  , _timer_start_ts_ms(0) {}

void FanController::init() {
  _stop_fan(true);
  _unset_timer(true);
  Blynk.virtualWrite(VPIN_VERSION, BLYNK_FIRMWARE_VERSION);
}

void FanController::run() {
  if (_timer_id >= 0) _timer.run();
}

void FanController::vpin_handler(uint8_t pin, BlynkParam const *param) {
  switch (pin) {
    case 0: { // ON/OFF
      int value = param->asInt();
      if (value == 0) {
        _stop_fan();
        _unset_timer(true);
      } else {
        _start_fan();
        if (_blk_timer_on == 1) _set_timer();
      }
      LOG_INFO(String("Fan on/off: ") + value);
      break;
    }
    case 1: { // Speed
      int value = param->asInt();
      if (value < MIN_SPEED) { value = MIN_SPEED; Blynk.virtualWrite(VPIN_FAN_SPEED, value); }
      if (value > MAX_SPEED) { value = MAX_SPEED; Blynk.virtualWrite(VPIN_FAN_SPEED, value); }
      _blk_speed = value;
      _apply_speed();
      break;
    }
    case 2: { // Timer ON/OFF
      int value = param->asInt();
      if (value == 0 && _blk_timer_on == 1) {
        _unset_timer();
      } else if (value == 1 && _blk_timer_on == 0) {
        _start_fan(true);
        _set_timer();
      }
      break;
    }
    case 3: { // Timer length
      _blk_timer_time = param->asInt();
      break;
    }
  }
}

void FanController::update_timer() {
  uint32_t now = millis();
  uint32_t elapse = now - _timer_start_ts_ms;

  if (elapse > 60000) {
    _timer_start_ts_ms = now - (elapse % 60000);
    _remaining_time -= (int)(elapse / 60000);

    if (_remaining_time <= 0) {
      _remaining_time = 0;
      _stop_fan(true);
      _unset_timer(true);
    } else {
      Blynk.virtualWrite(VPIN_TIMER_COUNTDOWN, _remaining_time);
    }
  }
}

void FanController::_start_fan(bool update_blynk) {
  fan_pwm_write((uint8_t)_blk_speed);
  _blk_switch_on = 1;
  if (update_blynk) Blynk.virtualWrite(VPIN_FAN_ON_OFF, 1);
}

void FanController::_stop_fan(bool update_blynk) {
  fan_pwm_write(0);
  _blk_switch_on = 0;
  _sm->stop();
  if (update_blynk) Blynk.virtualWrite(VPIN_FAN_ON_OFF, 0);
}

void FanController::_set_timer() {
  if (_timer_id >= 0) _unset_timer();
  _blk_timer_on = 1;
  _remaining_time = _blk_timer_time;
  _timer_start_ts_ms = millis();
  _timer_id = _timer.setInterval(1000, update_timer_cb, this);
  Blynk.virtualWrite(VPIN_TIMER_COUNTDOWN, _remaining_time);
  LOG_INFO(String("Fan timer set: ") + _blk_timer_time + " min");
}

void FanController::_unset_timer(bool update_blynk) {
  if (_timer_id >= 0) {
    _timer.deleteTimer(_timer_id);
    _timer_id = -1;
  }
  _remaining_time = 0;
  _blk_timer_on = 0;
  Blynk.virtualWrite(VPIN_TIMER_COUNTDOWN, 0);
  if (update_blynk) Blynk.virtualWrite(VPIN_TIMER_ON_OFF, 0);
}

void FanController::_apply_speed() {
  if (_blk_switch_on) fan_pwm_write((uint8_t)_blk_speed);
}

void update_timer_cb(void *p_fc) {
  ((FanController *)p_fc)->update_timer();
}

// ------------------------------------------------------------------ //
//  Globals                                                             //
// ------------------------------------------------------------------ //
StepperMotor  stepper_motor;
FanController fan_controller(&stepper_motor);
BlynkTimer    mainTimer;

// ------------------------------------------------------------------ //
//  Blynk callbacks                                                     //
// ------------------------------------------------------------------ //
BLYNK_WRITE_DEFAULT() {
  uint8_t pin = (uint8_t)request.pin;
  if (pin <= 6)
    fan_controller.vpin_handler(pin, &param);
  else if (pin >= 10 && pin <= 15)
    stepper_motor.vpin_handler(pin, &param);
}

BLYNK_CONNECTED() {
  DEBUG_PRINT("Blynk connected — syncing datastreams");
  Blynk.syncVirtual(VPIN_FAN_ON_OFF, VPIN_FAN_SPEED,
                    VPIN_TIMER_ON_OFF, VPIN_TIMER_LENGTH, VPIN_TIMER_COUNTDOWN,
                    VPIN_VERSION, VPIN_UPTIME);
  Blynk.syncVirtual(VPIN_SM_ON_OFF, VPIN_SM_SPEED,
                    VPIN_SM_MAX_ANGLE, VPIN_SM_CURRENT_ANGLE,
                    VPIN_SM_RESET_ANGLE, VPIN_SM_ANGLE_UPDATE);
  fan_controller.init();
  stepper_motor.init();
}

// ------------------------------------------------------------------ //
//  Uptime (every 5 minutes — one message, safe for monthly quota)     //
// ------------------------------------------------------------------ //
void uptimeEvent() {
  uint32_t total_s = (millis() - boot_ms) / 1000UL;
  uint32_t days =  total_s / 86400UL;
  uint32_t hrs  = (total_s % 86400UL) / 3600UL;
  uint32_t mins = (total_s % 3600UL)  / 60UL;

  char buf[24];
  if (days > 0)
    snprintf(buf, sizeof(buf), "%lud %02luh %02lum",
             (unsigned long)days, (unsigned long)hrs, (unsigned long)mins);
  else
    snprintf(buf, sizeof(buf), "%luh %02lum",
             (unsigned long)hrs, (unsigned long)mins);
  Blynk.virtualWrite(VPIN_UPTIME, buf);
}

// ------------------------------------------------------------------ //
//  Arduino entry points                                                //
// ------------------------------------------------------------------ //
void setup() {
  Serial.begin(115200);
  delay(100);

  // Stepper pins
  pinMode(SM_PIN0, OUTPUT);
  pinMode(SM_PIN1, OUTPUT);
  pinMode(SM_PIN2, OUTPUT);
  pinMode(SM_PIN3, OUTPUT);
  digitalWrite(SM_PIN0, LOW);
  digitalWrite(SM_PIN1, LOW);
  digitalWrite(SM_PIN2, LOW);
  digitalWrite(SM_PIN3, LOW);

  // Fan PWM
  fan_pwm_init();
  fan_pwm_write(0);

  // Nano watchdog: TX-only, 9600 baud
  Serial2.begin(9600, SERIAL_8N1, -1, WATCHDOG_HEARTBEAT_PIN);

  boot_ms = millis();

  // Uptime every 5 minutes
  mainTimer.setInterval(300000L, uptimeEvent);

  // Edgent handles WiFi provisioning, auth-token storage, OTA, and
  // the factory-reset button (GPIO0 held 5 s).
  BlynkEdgent.begin();
}

void loop() {
  BlynkEdgent.run();
  mainTimer.run();
  fan_controller.run();
  stepper_motor.run();

  // Nano watchdog heartbeat: "HB\n" every 2 s
  static uint32_t hb_last = 0;
  uint32_t hb_now = millis();
  if (hb_now - hb_last >= 2000) {
    hb_last = hb_now;
    Serial2.println("HB");
  }
}
