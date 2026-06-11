// Board: ESP32-C3 Super Mini
// Button: GPIO9 (BOOT button, internal pull-up — active low)
// LED:    GPIO8 (plain blue LED, active-LOW — LOW = on)
//
// NOTE: the stock Edgent example's USE_ESP32C3_DEV_MODULE branch assumes a
// WS2812 addressable LED on GPIO8. The Super Mini has a plain single-color LED,
// so we use BOARD_LED_PIN + BOARD_LED_INVERSE instead (no NeoPixel library).

/*
 * Board configuration
 */
#define BOARD_BUTTON_PIN              9
#define BOARD_BUTTON_ACTIVE_LOW       true

#define BOARD_LED_PIN                 8
#define BOARD_LED_INVERSE             true      // LOW turns the LED on
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
