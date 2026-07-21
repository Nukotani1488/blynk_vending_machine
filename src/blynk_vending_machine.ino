/*
  Author: Nukotani
*/

#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

#include "web_server.h"
#include "config.h"
#include <BlynkSimpleEsp32.h>



Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40); // default I2C address

uint16_t angleToTicks(uint8_t angle) {
  return map(angle, 0, 180, SERVO_MIN, SERVO_MAX);
}

int16_t extract_slot_from_value(uint32_t value) {
  uint8_t slot = EXTRACT_LOW_BITS(value, SLOT_COUNT);
  if (slot >= SLOT_COUNT) {
    Serial.print("Invalid slot number extracted from value: ");
    Serial.println(slot);
    return -1;
  }
  return slot;
}

uint32_t extract_price_from_value(uint32_t value, uint8_t slot) {
  return value - slot;
}

bool validate_price(uint32_t price, uint8_t slot) {
  //TODO: implement price validation logic
  return true;
}

// ---------- Dispense state machine ----------

enum DispenseState: uint8_t { IDLE = 0, MOVING_OUT, MOVING_BACK };

typedef struct {
  uint32_t state_start_time;
  DispenseState state;
  uint8_t pending_orders;
  uint8_t slot_id;
  uint8_t available_stock;
} Slot;

Slot slots[SLOT_COUNT] = {};

void initialize_slots() {
  for (int i = 0; i < SLOT_COUNT; i++) {
    slots[i].state_start_time = 0;
    slots[i].state = IDLE;
    slots[i].pending_orders = 0;
    slots[i].slot_id = i;
    slots[i].available_stock = 10; // Example initial stock, adjust as needed
  }
}

//DispenseState states[SLOT_COUNT] = {IDLE};
//uint32_t state_start_times[SLOT_COUNT] = {0};
//uint8_t slot_request_queue[SLOT_COUNT] = {0};

void moveServo(int channel, uint8_t angle) {
  pwm.setPWM(channel, 0, angleToTicks(angle));
}

bool start_dispense(int slot) {
  if (slots[slot].available_stock == 0) {
    Serial.print("Slot ");
    Serial.print(slot);
    Serial.println(" is out of stock");
    return false;
  }

  moveServo(slot, OUT_ANGLE);

  slots[slot].available_stock--;
  slots[slot].state_start_time = millis();
  slots[slot].state = MOVING_OUT;

  Serial.print("Dispensing slot ");
  Serial.println(slot);
  return true;
}

void process_orders() {
  for (int i = 0; i < SLOT_COUNT; i++) {
    if (slots[i].pending_orders > 0 && slots[i].state == IDLE) {

      if (slots[i].available_stock == 0) {
          Serial.print("Slot ");
          Serial.print(i);
          Serial.println(" has pending requests but no stock");
          continue;
      }

      if (start_dispense(i)) {
        slots[i].pending_orders--;
      }
    }
  }
}

void update_state_machine() {
  uint32_t current_time = millis();
  for (int i = 0; i < SLOT_COUNT; i++) {
    switch (slots[i].state) {
      case MOVING_OUT:
        if (current_time - slots[i].state_start_time >= MOVE_DURATION) {
          moveServo(i, REST_ANGLE);
          slots[i].state_start_time = current_time;
          slots[i].state = MOVING_BACK;
          Serial.print("Slot ");
          Serial.print(i);
          Serial.println(" moved out, returning to rest");
        }
        break;
      case MOVING_BACK:
        if (current_time - slots[i].state_start_time >= MOVE_DURATION) {
          slots[i].state = IDLE;
          Serial.print("Slot ");
          Serial.print(i);
          Serial.println(" returned to rest position");
        }
        break;
      default:
        break;
    }
  }
}

BLYNK_WRITE(V0) {
  int32_t _value = param.asInt();
  if (_value < 0) {
    Serial.println("Invalid value for V0, must be non-negative");
    return;
  }
  uint32_t value = (uint32_t)_value;

  int16_t slot = extract_slot_from_value(value);
  if (slot < 0) {
    Serial.print("Invalid slot number: ");
    Serial.println(slot);
    return;
  }
  Serial.print("Received dispense request for slot ");
  Serial.println(slot);

  uint32_t price = extract_price_from_value(value, slot);

  if (!validate_price(price, slot)) {
    Serial.print("Price validation failed for slot ");
    Serial.println(slot);
    return;
  }

  if (slots[slot].available_stock <= slots[slot].pending_orders) {
      Serial.print("No available stock for slot ");
      Serial.println(slot);
      return;
  }

  slots[slot].pending_orders++;

}

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WOKWI_SSID, WOKWI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print("*");
  }

  Serial.println("");
  Serial.println("WiFi connected");

  initialize_slots();

  pwm.begin();
  pwm.setPWMFreq(SERVO_FREQ);

  for (int i = 0; i < SLOT_COUNT; i++) {
    moveServo(i, REST_ANGLE);
  }

  Blynk.config(BLYNK_AUTH_TOKEN);
  Blynk.connect();

  Serial.println("Vending machine ready");
}

void loop() {
  Blynk.run();
  process_orders();
  update_state_machine();
}