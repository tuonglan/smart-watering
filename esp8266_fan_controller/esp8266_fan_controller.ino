// Template ID
#define BLYNK_TEMPLATE_ID "TMPL6PyORnvfe" // Template ID
#define BLYNK_TEMPLATE_NAME "Home Fan Controller" // Device Name
#define BLYNK_AUTH_TOKEN "" // Authentication number
#define BLYNK_FIRMWARE_VERSION "0.1.1"

#define LOCAL_WIFI_SSID "" //"Wifi SSID"
#define LOCAL_WIFI_PASS "" //"Wifi Pass"

// Comment this out to disable prints and save space
#define BLYNK_PRINT Serial

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>

#define PWM_PIN 3
#define SM_PIN0 15  // Stepper Motor pins
#define SM_PIN1 13
#define SM_PIN2 12
#define SM_PIN3 14
#define SM_MIN_DELAY 3
#define SM_MAX_SPEED 300 // Steps per seconds


// Pins for controlling fan
// #0: ON/OFF
// #1: Fan speed
// #2: Timer ON/OFF
// #3: Timer interval
// #4: Timer countdown
// #5: Reserved
class FanControllerVPins {
 public:
  enum VPins {
    ON_OFF = 0,
    SPEED = 1,
    TIMER_ON_OFF = 2,
    TIMER_LENGTH = 3,
    TIMER_COUNTDOWN = 4,
    VERSION = 5,
    RESERVED = 6
  };

  static size_t const vpin_count = 7;
};


class StepperMotorVPins {
 public:
  enum VPins {
    ON_OFF = 10,
    SPEED = 11,
    MAX_ANGLE = 12,
    CURRENT_ANGLE = 13,
    RESET_ANGLE = 14,
    RESERVED = 15,
  };

  static size_t const vpin_count = 6;
};
//uint8_t const FAN_BLYNK_VPINS[] = {0, 1, 2, 3, 4, 5};
//size_t const FAN_BLYNK_VPINS_N = sizeof(FAN_BLYNK_VPINS) / sizeof(uint8_t);
uint32_t const ULONG_MAX_VALUE = (uint32_t)(-1);


void update_timer_func(void *fc);
void update_step_func(void *sm);
void sm_step_func1(uint16_t step_idx);
void sm_step_func2(uint16_t step_idx);


// ******************************************************************************************************
// ***********************************------- Fain Controller ---*****************************************
class StepperMotor {
 public:
  StepperMotor(HardwareSerial *serial, BlynkWifi *blynk);
  ~StepperMotor();

  void init();
  void run();
  void vpin_handler(uint8_t pin, BlynkParam const *param);
  void step();
  void stop() { this->_unset_timer(true); }
 protected:
  void switch_on_handler(BlynkParam const *param);
  void speed_handler(BlynkParam const *param);
  void max_angle_handler(BlynkParam const *param);
  void current_angle_handler(BlynkParam const *param);
  void reset_angle_handler(BlynkParam const *param);

 private:
  void _step(uint16_t step_idx);
  void _set_speed(uint8_t speed) { _blk_speed = speed; }
  void _set_max_angle(uint16_t max_angle) { _blk_max_angle = max_angle; }
  void _set_current_angle(int16_t angle) { _blk_current_angle = angle; }
  void _reset_angle();

  void _set_timer(bool update_blynk=false);
  void _unset_timer(bool update_blynk=false);

  uint8_t _blk_switch_on;
  uint8_t _blk_speed;
  uint16_t _blk_max_angle;      // Measured in number of steps
  int16_t _blk_current_angle;  // Measured in number of steps

  bool _direction;              // True for clockwise and FAlse for counterclockwise
  uint32_t _step_ts_ms;

  int _timer_id;

  HardwareSerial *_serial;
  BlynkWifi *_blynk;
  BlynkTimer *_timer;
};


StepperMotor::StepperMotor(HardwareSerial *serial, BlynkWifi *blynk) 
  : _blynk(blynk), _serial(serial)
  , _timer_id(-1), _blk_current_angle(0), _direction(true) {

  _timer = new BlynkTimer();
}


StepperMotor::~StepperMotor() {
  delete _timer;
}


void StepperMotor::init() {
  this->_unset_timer(true);
}


void StepperMotor::run() {
  if (_timer_id >= 0)
    _timer->run();
}


void StepperMotor::vpin_handler(uint8_t pin, BlynkParam const *param) {
  if (StepperMotorVPins::ON_OFF == pin)
    this->switch_on_handler(param);
  else if (StepperMotorVPins::SPEED == pin)
    this->speed_handler(param);
  else if (StepperMotorVPins::MAX_ANGLE == pin)
    this->max_angle_handler(param);
  else if (StepperMotorVPins::CURRENT_ANGLE == pin)
    this->current_angle_handler(param);
  else if (StepperMotorVPins::RESET_ANGLE == pin)
    this->reset_angle_handler(param);
}


void StepperMotor::step() {
  // Decide the direction
  if (_blk_current_angle >= _blk_max_angle) {
    _blk_current_angle = _blk_max_angle;
    _direction = false;
  }
  else if (_blk_current_angle <= 0) {
    _blk_current_angle = 0;
    _direction = true;
  }

  // Increase or decrease angle
  if (_direction)
    _blk_current_angle++;
  else
    _blk_current_angle--;

  // Rotate 
  this->_step(_blk_current_angle);

  // Update current angle if neccessary
  uint32_t current_ts_ms = millis();
  int elapse = 0;

  if (current_ts_ms < _step_ts_ms)
    elapse = ULONG_MAX_VALUE - _step_ts_ms + current_ts_ms;
  else
    elapse = current_ts_ms - _step_ts_ms;
  if (elapse >= 1000) {
    this->_blynk->virtualWrite(StepperMotorVPins::CURRENT_ANGLE, _blk_current_angle);
    _step_ts_ms = current_ts_ms;
  }

//  this->_serial->print("The current angle: ");
//  this->_serial->println(_blk_current_angle);
//  this->_serial->print("Elapse: ");
//  this->_serial->println(elapse);
}


void StepperMotor::switch_on_handler(BlynkParam const *param) {
  int value = param->asInt();
  if (value == 0) {
    this->_unset_timer();
  }
  else if (value == 1) {
    this->_set_timer();
  }

  this->_serial->print("Rotation On/Off: ");
  this->_serial->println(value);
}


void StepperMotor::speed_handler(BlynkParam const *param) {
  int value = param->asInt();
  this->_set_speed(value);

  this->_serial->print("Rotation Speed set at: ");
  this->_serial->println(value);
}


void StepperMotor::max_angle_handler(BlynkParam const *param) {
  int value = param->asInt();
  this->_set_max_angle(value);
}


void StepperMotor::current_angle_handler(BlynkParam const *param) {
  int value = param->asInt();
  this->_set_current_angle(value);
}


void StepperMotor::reset_angle_handler(BlynkParam const *param) {
  int value = param->asInt();
  if (value == 1) {
    this->_reset_angle();
    this->_blynk->virtualWrite(StepperMotorVPins::RESET_ANGLE, 0);
  }
}


void StepperMotor::_reset_angle() {
  _blk_current_angle = 0;
  _direction = true;
  this->_blynk->virtualWrite(StepperMotorVPins::CURRENT_ANGLE, 0);
}


void StepperMotor::_set_timer(bool update_blynk) {
  if (_timer_id >= 0)
    this->_unset_timer();

  int interval = 1000 / _blk_speed;
  if (interval < SM_MIN_DELAY)
    interval = SM_MIN_DELAY;
  _step_ts_ms = millis();
  _blk_switch_on = 1;
  _timer_id = this->_timer->setInterval(interval, update_step_func, this);

  if (update_blynk)
    this->_blynk->virtualWrite(StepperMotorVPins::ON_OFF, 1);
}


void StepperMotor::_unset_timer(bool update_blynk) {
  if (_timer_id >= 0) {
    this->_timer->deleteTimer(_timer_id);
    _timer_id = -1;
  }
  _blk_switch_on = 0;

  // Stop the motor
  digitalWrite(SM_PIN0, LOW);
  digitalWrite(SM_PIN1, LOW);
  digitalWrite(SM_PIN2, LOW);
  digitalWrite(SM_PIN3, LOW);

  this->_blynk->virtualWrite(StepperMotorVPins::CURRENT_ANGLE, _blk_current_angle);

  if (update_blynk)
    this->_blynk->virtualWrite(StepperMotorVPins::ON_OFF, 0);
}


void StepperMotor::_step(uint16_t step_idx) {
  sm_step_func1(step_idx);
}


void sm_step_func2(uint16_t step_idx) {
  uint8_t phase = step_idx % 4;
  switch(phase) {
    case 0:
      digitalWrite(SM_PIN0, HIGH); 
      digitalWrite(SM_PIN1, LOW); 
      digitalWrite(SM_PIN2, HIGH); 
      digitalWrite(SM_PIN3, LOW); 
      break; 
    case 1:
      digitalWrite(SM_PIN0, LOW); 
      digitalWrite(SM_PIN1, HIGH); 
      digitalWrite(SM_PIN2, HIGH); 
      digitalWrite(SM_PIN3, LOW); 
      break; 
    case 2:
      digitalWrite(SM_PIN0, LOW); 
      digitalWrite(SM_PIN1, HIGH); 
      digitalWrite(SM_PIN2, LOW); 
      digitalWrite(SM_PIN3, HIGH); 
      break; 
    case 3:
      digitalWrite(SM_PIN0, HIGH); 
      digitalWrite(SM_PIN1, LOW); 
      digitalWrite(SM_PIN2, LOW); 
      digitalWrite(SM_PIN3, HIGH); 
      break; 
    default:
      digitalWrite(SM_PIN0, LOW); 
      digitalWrite(SM_PIN1, LOW); 
      digitalWrite(SM_PIN2, LOW); 
      digitalWrite(SM_PIN3, LOW); 
      break; 
  }
}


void sm_step_func1(uint16_t step_idx) {
  uint8_t phase = step_idx % 8;
  switch(phase) { 
    case 0: 
      digitalWrite(SM_PIN0, LOW); 
      digitalWrite(SM_PIN1, LOW); 
      digitalWrite(SM_PIN2, LOW); 
      digitalWrite(SM_PIN3, HIGH); 
      break; 
    case 1: 
      digitalWrite(SM_PIN0, LOW); 
      digitalWrite(SM_PIN1, LOW); 
      digitalWrite(SM_PIN2, HIGH); 
      digitalWrite(SM_PIN3, HIGH); 
      break; 
    case 2: 
      digitalWrite(SM_PIN0, LOW); 
      digitalWrite(SM_PIN1, LOW); 
      digitalWrite(SM_PIN2, HIGH); 
      digitalWrite(SM_PIN3, LOW); 
      break; 
    case 3: 
      digitalWrite(SM_PIN0, LOW); 
      digitalWrite(SM_PIN1, HIGH); 
      digitalWrite(SM_PIN2, HIGH); 
      digitalWrite(SM_PIN3, LOW); 
      break; 
    case 4: 
      digitalWrite(SM_PIN0, LOW); 
      digitalWrite(SM_PIN1, HIGH); 
      digitalWrite(SM_PIN2, LOW); 
      digitalWrite(SM_PIN3, LOW); 
      break; 
    case 5: 
      digitalWrite(SM_PIN0, HIGH); 
      digitalWrite(SM_PIN1, HIGH); 
      digitalWrite(SM_PIN2, LOW); 
      digitalWrite(SM_PIN3, LOW); 
      break; 
    case 6: 
      digitalWrite(SM_PIN0, HIGH); 
      digitalWrite(SM_PIN1, LOW); 
      digitalWrite(SM_PIN2, LOW); 
      digitalWrite(SM_PIN3, LOW); 
      break; 
    case 7: 
      digitalWrite(SM_PIN0, HIGH); 
      digitalWrite(SM_PIN1, LOW); 
      digitalWrite(SM_PIN2, LOW); 
      digitalWrite(SM_PIN3, HIGH); 
      break; 
    default: 
      digitalWrite(SM_PIN0, LOW); 
      digitalWrite(SM_PIN1, LOW); 
      digitalWrite(SM_PIN2, LOW); 
      digitalWrite(SM_PIN3, LOW); 
      break; 
  }
}


void update_step_func(void *p_sm) {
  StepperMotor *sm = (StepperMotor *)p_sm;
  sm->step();
}



// ******************************************************************************************************
// ***********************************------- Fain Controller ---*****************************************
class FanController {
 public:
  FanController(HardwareSerial *serial, BlynkWifi *blynk, StepperMotor *s_motor);
  ~FanController();

  void init();
  void run();
  void vpin_handler(uint8_t pin, BlynkParam const *param);
  void update_timer();
 protected:
  void run_timer();
  
  void switch_on_handler(BlynkParam const *param);
  void speed_handler(BlynkParam const *param);
  void timer_on_handler(BlynkParam const *param);
  void timer_time_handler(BlynkParam const *param);

 private:
  void _start_fan(bool update_blynk=false);
  void _stop_fan(bool update_blynk=false);
  void _set_timer();
  void _unset_timer(bool update_blynk=false);
  void _set_speed();

  static uint8_t const MIN_SPEED = 32;
  static uint8_t const MAX_SPEED = 255;

//  uint8_t *_vpins;
//  size_t _n_vpins;

  StepperMotor *_s_motor;
  HardwareSerial *_serial;
  BlynkWifi *_blynk;
  BlynkTimer *_timer;
  int _timer_id;

  // Stream data
  int _blk_switch_on;
  int _blk_speed;
  int _blk_timer_on;
  int _blk_timer_time;

  // Data for status
  int _remaining_time;
  uint32_t _timer_start_ts_ms;
};


FanController::FanController(HardwareSerial *serial, BlynkWifi *blynk, StepperMotor *s_motor)
  : _serial(serial), _blynk(blynk), _s_motor(s_motor) {

  // Init vpins
//  _n_vpins = sizeof(vpins) / sizeof(uint8_t);
//  _vpins = new uint8_t[_n_vpins];
//  for (int i=0;i<_n_vpins;++i)
//    _vpins[i] = vpins[i];

  // Init timer
  _timer = new BlynkTimer();
  _timer_id = -1;
}


FanController::~FanController() {
//  delete[] _vpins;
  delete _timer;
}


void FanController::init() {
  this->_stop_fan(true);
  this->_unset_timer(true);

  this->_blynk->virtualWrite(FanControllerVPins::VERSION, BLYNK_FIRMWARE_VERSION);
}


void FanController::run() {
  this->run_timer();
}


void FanController::run_timer() {
  if (_timer_id >= 0)
    _timer->run();
}


void FanController::vpin_handler(uint8_t pin, BlynkParam const *param) {
  switch(pin) {
    case FanControllerVPins::ON_OFF :
      this->switch_on_handler(param);
      break;
    case FanControllerVPins::SPEED :
      this->speed_handler(param);
      break;
    case FanControllerVPins::TIMER_ON_OFF :
      this->timer_on_handler(param);
      break;
    case FanControllerVPins::TIMER_LENGTH :
      this->timer_time_handler(param);
      break;
    default:
      break;
  }
}


void FanController::update_timer() {
  uint32_t current_ts_ms = millis();
  uint32_t elapse = 0;

//  this->_serial->print("Updating timer...\nCurrent ts_ms: ");
//  this->_serial->println(current_ts_ms);
//  this->_serial->print("Current remaining time: ");
//  this->_serial->println(_remaining_time);

  // Calculate the elapse
  if (current_ts_ms < _timer_start_ts_ms) {
    elapse = ULONG_MAX_VALUE - _timer_start_ts_ms + current_ts_ms;
  }
  else
    elapse = current_ts_ms - _timer_start_ts_ms;

  // Update timer
  if (elapse > 60000) {
    _timer_start_ts_ms = current_ts_ms + (elapse % 60000);
    _remaining_time -= elapse / 60000;

    if (_remaining_time <= 0) {
      _remaining_time = 0;
      this->_stop_fan(true);
      this->_unset_timer(true);
    }
    else
      // Update the timer
      this->_blynk->virtualWrite(FanControllerVPins::TIMER_COUNTDOWN, _remaining_time);
  }
}


void FanController::switch_on_handler(BlynkParam const *param) {
  int value = param->asInt();
  if (value == 0) {
    this->_stop_fan();

    this->_unset_timer(true);
  }
  else if (value == 1) {
    this->_start_fan();

    if (_blk_timer_on == 1)
      this->_set_timer();
  }

  this->_serial->print("On/Off switch: ");
  this->_serial->println(value);
}


void FanController::speed_handler(BlynkParam const *param) {
  int value = param->asInt();
  if (value < FanController::MIN_SPEED) {
    value = FanController::MIN_SPEED;
    this->_blynk->virtualWrite(FanControllerVPins::SPEED, value);
  }
  if (value > FanController::MAX_SPEED) {
    value = FanController::MAX_SPEED;
    this->_blynk->virtualWrite(FanControllerVPins::SPEED, value);
  }

  _blk_speed = value;
  this->_set_speed();
}


void FanController::timer_on_handler(BlynkParam const *param) {
  int value = param->asInt();
  if (value == 0 && _blk_timer_on == 1) {
    this->_unset_timer();
  }
  else if (value == 1 && _blk_timer_on == 0) {
    this->_start_fan(true);
    this->_set_timer();
  }
}


void FanController::timer_time_handler(BlynkParam const *param) {
  int value = param->asInt();
  _blk_timer_time = value;
}


void FanController::_start_fan(bool update_blynk) {
  analogWrite(PWM_PIN, _blk_speed);
  _blk_switch_on = 1;

  if (update_blynk)
    this->_blynk->virtualWrite(FanControllerVPins::ON_OFF, 1);
}


void FanController::_stop_fan(bool update_blynk) {
  analogWrite(PWM_PIN, 0);
  _blk_switch_on = 0;
  _s_motor->stop();

  if (update_blynk)
    this->_blynk->virtualWrite(FanControllerVPins::ON_OFF, 0);
}


void FanController::_set_timer() {
  if (_timer_id >= 0)
    this->_unset_timer();

  _blk_timer_on = 1;
  _timer_id = this->_timer->setInterval(1000, update_timer_func, this);
  _remaining_time = _blk_timer_time;
  _timer_start_ts_ms = millis();

  // Update blynk count down
  this->_blynk->virtualWrite(FanControllerVPins::TIMER_COUNTDOWN, _remaining_time);

  this->_serial->print("Timer set: ");
  this->_serial->println(_timer_id);
}


void FanController::_unset_timer(bool update_blynk) {
  int timer_id = _timer_id;

  if (_timer_id >= 0) {
    this->_timer->deleteTimer(_timer_id);
    _timer_id = -1;
  }
  _remaining_time = 0;
  _blk_timer_on = 0;

  // Update blynk count down
  this->_blynk->virtualWrite(FanControllerVPins::TIMER_COUNTDOWN, _remaining_time);
  if (update_blynk)
    this->_blynk->virtualWrite(FanControllerVPins::TIMER_ON_OFF, 0);


  this->_serial->print("Timer unset: ");
  this->_serial->println(timer_id);
}


void FanController::_set_speed() {
  analogWrite(PWM_PIN, _blk_speed);
}


void update_timer_func(void *p_fc) {
  FanController *fc = (FanController *)p_fc;
  fc->update_timer();
}



// ******************************************************************************************************
// ***********************************------ Blynk Initalize ---*****************************************
StepperMotor stepper_motor(&Serial, &Blynk);
FanController fan_controller(&Serial, &Blynk, &stepper_motor);

BLYNK_WRITE_DEFAULT() {
  uint8_t pin = (uint8_t)request.pin;

  if (pin >= FanControllerVPins::ON_OFF && pin <= FanControllerVPins::RESERVED)
    fan_controller.vpin_handler(pin, &param);
  else if (pin >= StepperMotorVPins::ON_OFF && pin <= StepperMotorVPins::RESERVED)
    stepper_motor.vpin_handler(pin, &param);
}


BLYNK_CONNECTED() {
  Blynk.syncVirtual(FanControllerVPins::ON_OFF);
  Blynk.syncVirtual(FanControllerVPins::SPEED);
  Blynk.syncVirtual(FanControllerVPins::TIMER_ON_OFF);
  Blynk.syncVirtual(FanControllerVPins::TIMER_LENGTH);
  Blynk.syncVirtual(FanControllerVPins::TIMER_COUNTDOWN);
  Blynk.syncVirtual(FanControllerVPins::VERSION);
  Blynk.syncVirtual(FanControllerVPins::RESERVED);

  Blynk.syncVirtual(StepperMotorVPins::ON_OFF);
  Blynk.syncVirtual(StepperMotorVPins::SPEED);
  Blynk.syncVirtual(StepperMotorVPins::MAX_ANGLE);
  Blynk.syncVirtual(StepperMotorVPins::CURRENT_ANGLE);
  Blynk.syncVirtual(StepperMotorVPins::RESET_ANGLE);
  Blynk.syncVirtual(StepperMotorVPins::RESERVED);
}

// ******************************************************************************************************
// ***********************************------- Main functions ---*****************************************
void setup() {
  // put your setup code here, to run once:
//  Serial.begin(115200); // ESP01/ESP01S
  Serial.begin(9600);   // ESP8266 MCU
//  Serial.setDebugOutput(true);
  pinMode(PWM_PIN, OUTPUT);
  pinMode(SM_PIN0, OUTPUT);
  pinMode(SM_PIN1, OUTPUT);
  pinMode(SM_PIN2, OUTPUT);
  pinMode(SM_PIN3, OUTPUT);

  Blynk.begin(BLYNK_AUTH_TOKEN, LOCAL_WIFI_SSID, LOCAL_WIFI_PASS);

  fan_controller.init();
  stepper_motor.init();
}

void loop() {
  // put your main code here, to run repeatedly:
  Blynk.run();
  fan_controller.run();
  stepper_motor.run();
}
