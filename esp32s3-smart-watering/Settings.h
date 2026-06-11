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

#if defined(APP_DEBUG)
  #define DEBUG_PRINT(...)  BLYNK_LOG1(__VA_ARGS__)
  #define DEBUG_PRINTF(...) BLYNK_LOG(__VA_ARGS__)
#else
  #define DEBUG_PRINT(...)
  #define DEBUG_PRINTF(...)
#endif
