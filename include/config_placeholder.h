#define SLOT_COUNT 8
#define BITS_FOR_SIZE(size) (32 - __builtin_clz((size) - 1))
#define EXTRACT_LOW_BITS(value, size) ((value) & ((1u << BITS_FOR_SIZE(size)) - 1))

#define SERVO_FREQ 50
#define SERVO_MIN  150   // ~1ms pulse -> approx 0 degrees (tune per servo)
#define SERVO_MAX  600   // ~2ms pulse -> approx 180 degrees (tune per servo)
#define MOVE_DURATION 500

#define REST_ANGLE 0
#define OUT_ANGLE  90

//#define WOKWI_SSID "Wokwi-GUEST"
#define WOKWI_SSID "IMAM_SITI 2"
#define WOKWI_PASS "88888888"

//#define BLYNK_TEMPLATE_ID "TMPL6esf4JV1y"
//#define BLYNK_TEMPLATE_NAME "Vending Machine"
//#define BLYNK_AUTH_TOKEN "tbQ_cnMIvsIcgpW98yphaj1IvznsNDdO"

#define BLYNK_TEMPLATE_ID "TMPL6esf4JV1y"
#define BLYNK_TEMPLATE_NAME "Vending Machine"
#define BLYNK_AUTH_TOKEN "a5BBqxG69BtMpd-Vy3CyT8VzcMFLL7Ta"