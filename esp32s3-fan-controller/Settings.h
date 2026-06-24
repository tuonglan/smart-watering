// Board: UICPAL ESP32-S3-CAM N16R8 (ESP32-S3-WROOM-1, 16 MB flash / 8 MB OPI PSRAM)
// Button: GPIO0  (BOOT button, internal pull-up — active low)
// LED:    GPIO48 (onboard addressable WS2812 RGB LED)
//
// Fan PWM:  GPIO15
// Stepper:  GPIO4 / GPIO5 / GPIO6 / GPIO7

/*
 * Board configuration
 */
#define BOARD_BUTTON_PIN              0
#define BOARD_BUTTON_ACTIVE_LOW       true

#define BOARD_LED_PIN_WS2812          48        // onboard addressable RGB LED
#define BOARD_LED_BRIGHTNESS          64

// Watchdog heartbeat to external Arduino Nano guardian (nano-watchdog/).
// GPIO21 TX-only; the Nano receives "HB\n" every 2 s.
#define WATCHDOG_HEARTBEAT_PIN        21

/*
 * Advanced options (required by the Edgent helper files — keep all of these)
 */
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

#define USE_PTHREAD

#define BLYNK_NO_DEFAULT_BANNER

// ------------------------------------------------------------------ //
//  Application logging — runtime severity levels (V40-compatible)     //
// ------------------------------------------------------------------ //
#define LOG_LEVEL_OFF    0
#define LOG_LEVEL_ERROR  1
#define LOG_LEVEL_WARN   2
#define LOG_LEVEL_INFO   3
#define LOG_LEVEL_DEBUG  4

#if defined(APP_DEBUG)
  extern uint8_t g_logLevel;

  #define LOG_ERROR(...)    do { if (g_logLevel >= LOG_LEVEL_ERROR) BLYNK_LOG1(__VA_ARGS__); } while (0)
  #define LOG_WARN(...)     do { if (g_logLevel >= LOG_LEVEL_WARN)  BLYNK_LOG1(__VA_ARGS__); } while (0)
  #define LOG_INFO(...)     do { if (g_logLevel >= LOG_LEVEL_INFO)  BLYNK_LOG1(__VA_ARGS__); } while (0)
  #define DEBUG_PRINT(...)  do { if (g_logLevel >= LOG_LEVEL_DEBUG) BLYNK_LOG1(__VA_ARGS__); } while (0)

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
