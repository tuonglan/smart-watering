// Board: UICPAL ESP32-S3-CAM N16R8 (ESP32-S3-WROOM-1, 16 MB flash / 8 MB OPI PSRAM)
// Button: GPIO0  (BOOT button, internal pull-up — active low)
// LED:    GPIO48 (onboard addressable WS2812 RGB LED)
//
// NOTE: this board has a single addressable WS2812 RGB LED (not a plain LED like
// the ESP32-C3 Super Mini). We drive it through BOARD_LED_PIN_WS2812, which makes
// Indicator.h pull in the Adafruit_NeoPixel library — install it via the Library
// Manager ("Adafruit NeoPixel"). The status colours (blue/green/red/magenta) then
// follow the normal Edgent connect/run/OTA patterns.

/*
 * Board configuration
 */
#define BOARD_BUTTON_PIN              0
#define BOARD_BUTTON_ACTIVE_LOW       true

#define BOARD_LED_PIN_WS2812          48        // onboard addressable RGB LED
#define BOARD_LED_BRIGHTNESS          64


/*
 * Advanced options (required by the Edgent helper files — keep all of these)
 */

// Button: warn (LED flashes) at 3 s, factory-reset at 5 s
#define BUTTON_HOLD_TIME_INDICATION   3000
#define BUTTON_HOLD_TIME_ACTION       5000
#define BUTTON_PRESS_TIME_ACTION      50

#define BOARD_PWM_MAX                 1023

#define BOARD_LEDC_CHANNEL_1          1
#define BOARD_LEDC_CHANNEL_2          2
#define BOARD_LEDC_CHANNEL_3          3
#define BOARD_LEDC_TIMER_BITS         10
#define BOARD_LEDC_BASE_FREQ          12000

#if !defined(CONFIG_VENDOR_PREFIX)
  #if defined(CONFIG_DEVICE_PREFIX)
    #define CONFIG_VENDOR_PREFIX      CONFIG_DEVICE_PREFIX
  #else
    #define CONFIG_VENDOR_PREFIX      "Blynk"
  #endif
#endif
#if !defined(CONFIG_AP_URL)
#define CONFIG_AP_URL                 "blynk.setup"
#endif
#if !defined(CONFIG_DEFAULT_SERVER)
#define CONFIG_DEFAULT_SERVER         "blynk.cloud"
#endif
#if !defined(CONFIG_DEFAULT_PORT)
#define CONFIG_DEFAULT_PORT           443
#endif

#define WIFI_CLOUD_MAX_RETRIES        500
#define WIFI_NET_CONNECT_TIMEOUT      50000
#define WIFI_CLOUD_CONNECT_TIMEOUT    50000
#define WIFI_AP_IP                    IPAddress(192, 168, 4, 1)
#define WIFI_AP_Subnet                IPAddress(255, 255, 255, 0)
//#define WIFI_CAPTIVE_PORTAL_ENABLE

#define USE_PTHREAD

#define BLYNK_NO_DEFAULT_BANNER

// ------------------------------------------------------------------ //
//  Application logging — runtime severity levels (V40)                 //
// ------------------------------------------------------------------ //
//  APP_DEBUG is the compile-time master kill-switch: if it is NOT
//  defined, every LOG_* macro below compiles to nothing (zero flash,
//  zero CPU) for a production build, and V40 does nothing.
//
//  When APP_DEBUG IS defined, g_logLevel (driven by virtual pin V40,
//  default 0/OFF at boot, synced from the app on connect) selects how
//  much OF OUR OWN application logging is emitted. It is a threshold:
//  a line prints when g_logLevel >= the line's severity.
//
//      V40  g_logLevel        emits (cumulative)
//      ---  ----------------  -----------------------------
//       0   LOG_LEVEL_OFF     nothing from our code
//       1   LOG_LEVEL_ERROR   + LOG_ERROR
//       2   LOG_LEVEL_WARN    + LOG_WARN
//       3   LOG_LEVEL_INFO    + LOG_INFO
//       4   LOG_LEVEL_DEBUG   + DEBUG_PRINT (everything, verbose)
//
//  This ONLY gates our logs. The ESP32 ROM boot messages and the Blynk
//  library's own output (banner, state) go through their own paths and
//  always print, regardless of V40.
#define LOG_LEVEL_OFF    0
#define LOG_LEVEL_ERROR  1
#define LOG_LEVEL_WARN   2
#define LOG_LEVEL_INFO   3
#define LOG_LEVEL_DEBUG  4

#if defined(APP_DEBUG)
  extern uint8_t g_logLevel;   // defined in the .ino, set at runtime via V40

  // String-style (single arg, e.g. String("x ") + y) -> BLYNK_LOG1
  #define LOG_ERROR(...)    do { if (g_logLevel >= LOG_LEVEL_ERROR) BLYNK_LOG1(__VA_ARGS__); } while (0)
  #define LOG_WARN(...)     do { if (g_logLevel >= LOG_LEVEL_WARN)  BLYNK_LOG1(__VA_ARGS__); } while (0)
  #define LOG_INFO(...)     do { if (g_logLevel >= LOG_LEVEL_INFO)  BLYNK_LOG1(__VA_ARGS__); } while (0)
  #define DEBUG_PRINT(...)  do { if (g_logLevel >= LOG_LEVEL_DEBUG) BLYNK_LOG1(__VA_ARGS__); } while (0)

  // printf-style (format + args) -> BLYNK_LOG
  #define LOG_ERRORF(...)   do { if (g_logLevel >= LOG_LEVEL_ERROR) BLYNK_LOG(__VA_ARGS__); } while (0)
  #define LOG_WARNF(...)    do { if (g_logLevel >= LOG_LEVEL_WARN)  BLYNK_LOG(__VA_ARGS__); } while (0)
  #define LOG_INFOF(...)    do { if (g_logLevel >= LOG_LEVEL_INFO)  BLYNK_LOG(__VA_ARGS__); } while (0)
  #define DEBUG_PRINTF(...) do { if (g_logLevel >= LOG_LEVEL_DEBUG) BLYNK_LOG(__VA_ARGS__); } while (0)
#else
  #define LOG_ERROR(...)
  #define LOG_WARN(...)
  #define LOG_INFO(...)
  #define DEBUG_PRINT(...)
  #define LOG_ERRORF(...)
  #define LOG_WARNF(...)
  #define LOG_INFOF(...)
  #define DEBUG_PRINTF(...)
#endif
