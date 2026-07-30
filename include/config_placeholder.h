#define SLOT_COUNT 8
#define BITS_FOR_SIZE(size) (32 - __builtin_clz((size) - 1))
#define EXTRACT_LOW_BITS(value, size) ((value) & ((1u << BITS_FOR_SIZE(size)) - 1))

#define SERVO_FREQ 50
#define SERVO_MIN  150   // ~1ms pulse -> approx 0 degrees (tune per servo)
#define SERVO_MAX  600   // ~2ms pulse -> approx 180 degrees (tune per servo)
#define MOVE_DURATION 500

#define REST_ANGLE 0
#define OUT_ANGLE  90

#define DEFAULT_AP_SSID ""
#define DEFAULT_AP_PASSWORD ""

#define BAUD_RATE 115200

#define LOG_FLUSH_INTERVAL_MS 5000
#define LOG_BUFFER_MAX_SIZE 64