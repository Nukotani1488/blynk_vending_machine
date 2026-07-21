// Wokwi Custom Chip - For information and examples see:
// https://link.wokwi.com/custom-chips-alpha
//
// SPDX-License-Identifier: MIT
// Copyright (C) 2022 Bonny Rais

#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include "pca9685.h"

#define DEBUG 1
bool debug_timer = true;
bool i2c_debug = true;
bool gen_debug;

// --------------- Debug Macros -----------------------
#ifdef DEBUG
#define DEBUGF(...)      { if (gen_debug) {printf(__VA_ARGS__);} }
#define I2C_DEBUGF(...)  { if (i2c_debug) {printf(__VA_ARGS__);}  }
#endif //  DEBUG
// --------------- Debug Macros -----------------------

#define MICROS_IN_SEC 1000000

#define MIN_ADDR 1
#define MAX_ADDR 62
#define MAX_ADDR_PINS 6
#define ADDR_MASK 0x3F 
#define MAX_PIN_NAME_LEN 8
#define I2C_BUFFER_LEN 128

// address values defined here are special and do not match address pins or regular address.
// the values are intended to be matched by logic handling code.
#define ADDR_GEN_CALL 0
#define ADDR_SOFTWARE_RESET 6
#define ADDR_LED_ALL_CALL 101   // todo(bonnyr): fix this

// timer values
#define CHAN_TIMER_MASK 0x0FFF;
#define CHAN_TIMER_MAX_VALUE 0x1000 // 12 bit counter, 4096 values
#define OSC_FREQ 25000000

// Channel definitions
#define MAX_CHANNELS 16

// --- command registers / commands
#define MODE1 0x0         // read/write Mode register 1
#define MODE2 0x1         // read/write Mode register 2
#define SUBADR1 0x2       // read/write I2C-bus subaddress 1
#define SUBADR2 0x3       // read/write I2C-bus subaddress 2
#define SUBADR3 0x4       // read/write I2C-bus subaddress 3
#define ALLCALLADR 0x5    // read/write LED All Call I2C-bus address
#define LED_PWM_START 0x6 // start address of all LED PWMs - there are 64 (16 * 4) such addresses
                          // organised as ON_LOW, ON_HIGH, OFF_LOW , OFF_HIGH for each of the 16 channls
#define LED_PWM_END 0x45  // lass address of all LED PWMs - there are 64 (16 * 4) such addresses

#define ALL_LED_ON_L 0xFA  // write/read 0
#define ALL_LED_ON_H 0xFB  // write/read 1
#define ALL_LED_OFF_L 0xFC // write/read 0
#define ALL_LED_OFF_H 0xFD // write/read 1
#define PRE_SCALE 0xFE     //[1] read/write prescaler for PWM output
#define TEST_MODE 0xFF     //[2] read/write defines the test mode to be entered

// MODE1 commands
#define MODE1_RESTART 0x80
#define MODE1_EXTCLK 0x40
#define MODE1_AI 0x20
#define MODE1_SLEEP 0x10
#define MODE1_SUB1 0x08
#define MODE1_SUB2 0x04
#define MODE1_SUB3 0x02
#define MODE1_ALLCALL 0x01
#define MODE1_DEFAULT (MODE1_AI | MODE1_SLEEP | MODE1_ALLCALL)

// MODE2 command bits
#define MODE2_INVERT 0x10
#define MODE2_OCH 0x08
#define MODE2_OUTDRV 0x04
#define MODE2_OUTNE_H 0x02
#define MODE2_OUTNE_L 0x01
#define MODE2_DEFAULT (MODE2_OUTDRV)

typedef enum pin_state
{
  ST_PIN_OFF, // PWM disabled, PIN set to LOW
  ST_PIN_ON,  // PWM disabled, PIN set to HIGH
  ST_PWM,     // pin is PWM controlled
} pin_state_t;

typedef struct chip_desc chip_desc_t;

typedef struct chan_desc
{
  char name[MAX_PIN_NAME_LEN];
  pin_state_t state;
  uint16_t next;      // next event in the current cycle of 4096
  uint16_t on_time;   // duration (0 - 4095) that pin is set to high
  uint16_t off_time;  // duration (0 - 4095) that pin is set to low
  pin_t pin;          // the pin we're controlling
  bool pin_level;     // remember pin level

  // todo(bonnyr): evaluate whether this is better
  uint32_t timer;
  chip_desc_t *chip;
} chan_desc_t;

typedef struct chip_desc
{
  chan_desc_t channels[MAX_CHANNELS];
  uint32_t addr;
  pin_t addr_pins[MAX_ADDR_PINS];

  // timer
  uint64_t last_start;  // the last start time - used by channel timers to sync
  uint32_t timer;
  uint16_t next; // next event in the current cycle of 4096

  // i2c comms
  bool i2c_gen_call; // was the last command a general call ?
  bool i2c_type;     // read = true, write = false
  uint8_t i2c_buffer[I2C_BUFFER_LEN];
  uint8_t i2c_ndx;
  uint8_t i2c_state; // read or write indicated

  // control register
  uint8_t ctrl_reg;

  // mode registers
  uint8_t mode1;
  uint8_t mode2;

  // prescale
  uint8_t prescale;
  uint32_t duration_us; // a derived field to convert prescale value to timer base value in microseconds

} chip_desc_t;

// --- i2c handlers
static bool on_i2c_connect(void *user_data, uint32_t address, bool connect);
static uint8_t on_i2c_read(void *user_data);
static bool on_i2c_write(void *user_data, uint8_t data);
static void on_i2c_disconnect(void *user_data);

// --- event handlers
static void on_addr_pin_chg(void *user_data, pin_t pin, uint32_t value);
static void on_timer_event(void *data);
static void on_chan_timer_event(void *data);
static void on_i2c_event(chip_desc_t *chip);
static void on_gen_call_cmd(chip_desc_t *chip);
static void on_mode1(chip_desc_t *chip);
static void on_mode2(chip_desc_t *chip);
static void on_prescale(chip_desc_t *chip);
static void on_chan_pwm(chip_desc_t *chip);
static void on_all_chan_pwm(chip_desc_t *chip);

// --- util functions
static void chip_reset(chip_desc_t *chip);
static inline uint8_t addr_pins_to_addr(chip_desc_t *chip);
static void sched_timer(chip_desc_t *chip);
static void sched_chan_timers(chip_desc_t *chip);
static void set_chan_pwm(chip_desc_t *chip, chan_desc_t *chan);
static void set_prescale(chip_desc_t *chip, uint8_t prescale);

void chip_init()
{
  setvbuf(stdout, NULL, _IOLBF, 1024); // Limit output buffering to a single line

  chip_desc_t *chip = calloc(1, sizeof(chip_desc_t));

  // read config attributes
  uint32_t attr;
  
  attr = attr_init("i2c_debug", false); i2c_debug = attr_read(attr) != 0;
  attr = attr_init("gen_debug", false); gen_debug = attr_read(attr) != 0;
  attr = attr_init("debug_timer", false); debug_timer = attr_read(attr) != 0;

  // initialise channels
  for (int i = 0; i < MAX_CHANNELS; i++)
  {
    chan_desc_t *chan = chip->channels + i;
    snprintf(chan->name, MAX_PIN_NAME_LEN - 1, "LED%d", i);
    chan->pin = pin_init(chan->name, OUTPUT_LOW); // todo(bonnyr) confirm initial default value
    
    const timer_config_t timer_config = {
        .callback = on_chan_timer_event,
        .user_data = chan,
    };
    chan->timer = timer_init(&timer_config);
    chan->chip = chip;
  }

  // todo(bonnyr): initialise address pins to designate address.
  // Note: some pin permutations have special meaning other than i2c address values, so we must handle them here
  for (int i = 0; i < MAX_ADDR_PINS; i++)
  {
    char buf[4];
    pin_watch_config_t c = {
        .edge = BOTH,
        .pin_change = on_addr_pin_chg,
        .user_data = chip};

    snprintf(buf, 3, "A%d", i);
    chip->addr_pins[i] = pin_init(buf, INPUT_PULLDOWN); // todo(bonnyr) confirm initial default value
    pin_watch(chip->addr_pins[i], &c);
  }
  chip->addr = addr_pins_to_addr(chip);
  DEBUGF("pca9685 chip listening at addr %d\n", chip->addr);

  // initialise i2c
  const i2c_config_t i2c_config = {
      .user_data = chip,
      .address = 0,
      .scl = pin_init("SCL", INPUT),
      .sda = pin_init("SDA", INPUT),
      .connect = on_i2c_connect,
      .read = on_i2c_read,
      .write = on_i2c_write,
      .disconnect = on_i2c_disconnect, // Optional
  };
  i2c_dev_t d = i2c_init(&i2c_config);
  DEBUGF("i2c device: %d\n", d)

  // timer config
  const timer_config_t timer_config = {
      .callback = on_timer_event,
      .user_data = chip,
  };
  chip->timer = timer_init(&timer_config);

  chip_reset(chip);

  // The following message will appear in the browser's DevTools console:
  printf("PCA9685 chip initialised.\n");
}

static void chip_reset(chip_desc_t *chip)
{
  I2C_DEBUGF("chip_reset\n");

  chip->mode1 = MODE1_DEFAULT;
  chip->mode2 = MODE2_DEFAULT;

  // reset prescaler  and stop timer
  set_prescale(chip, 255);
  timer_stop(chip->timer);

  // all channel pins are turned off and PWM is reset
  for (int i = 0; i < MAX_CHANNELS; i++)
  {
    chan_desc_t *chan = chip->channels + i;
    chan->on_time = 0;
    chan->off_time = 0;
    chan->state = ST_PIN_OFF;
    chan->pin_level = LOW;
    pin_write(chan->pin, chan->pin_level);
    timer_stop(chan->timer);
  }
}

// --- I2C handling ---

bool on_i2c_connect(void *data, uint32_t address, bool read)
{
  I2C_DEBUGF("on_i2c_connect: %s addr %d\n", read ? "read" : "write", address);
  chip_desc_t *chip = data;

  chip->i2c_ndx = 0;
  memset(chip->i2c_buffer, 0, I2C_BUFFER_LEN);

  // check for reset first - proceeded by General Call Address (0x00)
  if (address == ADDR_GEN_CALL || ((address & ADDR_MASK) == chip->addr))
  {
    chip->i2c_gen_call = address == ADDR_GEN_CALL;
    chip->i2c_type = read;
    return true; /* Ack */
  }

  return false; /* NAck */
}

uint8_t on_i2c_read(void *data)
{
  chip_desc_t *chip = data;

  switch (chip->ctrl_reg)
  {
  case MODE1:
    I2C_DEBUGF("on_i2c_read mode1: %X\n", chip->mode1);
    return chip->mode1;
  case MODE2:
    I2C_DEBUGF("on_i2c_read mode2: %X\n", chip->mode2);
    return chip->mode2;
  case PRE_SCALE:
    I2C_DEBUGF("on_i2c_read prescale: %X\n", chip->prescale);
    return chip->prescale;
  }
  I2C_DEBUGF("on_i2c_read: unhandled read of reg %X\n", chip->ctrl_reg);
  return 0;
}

bool on_i2c_write(void *data, uint8_t data_byte)
{
  I2C_DEBUGF("on_i2c_write: data %X\n", data_byte);

  chip_desc_t *chip = data;
  if (chip->i2c_ndx >= I2C_BUFFER_LEN)
  {
    printf("err: too many bytes written to i2c buffer, resetting\n");
    chip_reset(chip);
    return false;
  }

  chip->i2c_buffer[chip->i2c_ndx++] = data_byte;
  return true; // Ack
}

void on_i2c_disconnect(void *data)
{
  I2C_DEBUGF("on_i2c_disconnect\n");
  chip_desc_t *chip = data;

  // if it's read - we're done
  if (chip->i2c_type == 1)
  {
    return;
  }

  // do_sm_event(chip, EV_I2C_BYTES_WRITTEN, &data);
  on_i2c_event(chip);

  chip->i2c_ndx = 0;
  memset(chip->i2c_buffer, 0, I2C_BUFFER_LEN);
}

static void on_addr_pin_chg(void *data, pin_t pin, uint32_t value)
{
  chip_desc_t *chip = data;

  chip->addr = addr_pins_to_addr(chip);

  // todo(bonnyr): handle special addresses configured by MCU
  // if (addr_val == ADDR_SOFTWARE_RESET) {
  //   on_software_reset(chip);
  //   return;
  // }
  // if (addr_val == ADDR_LED_ALL_CALL ) {
  //   on_led_all_call(chip);
  // }

  // ignore regular addresses - this chip's configured address will be used for i2c
}

static void on_software_reset(chip_desc_t *chip)
{
  printf("software reset not impl yet \n");
}

static void on_led_all_call(chip_desc_t *chip)
{
  printf("led_all_call not impl yet \n");
}

static void on_i2c_event(chip_desc_t *chip)
{
  // i2c bytes have been written to buffer, parse and execute

  // check for general address command
  if (chip->i2c_gen_call)
  {
    on_gen_call_cmd(chip);
    return;
  }

  // handle regular address command
  chip->ctrl_reg = chip->i2c_buffer[0];
  // DEBUGF("on_i2c_event: ctrl_reg: %X\n", chip->ctrl_reg);

  // prefix read writes the control register, then performs a read.
  // check whether this is the case
  if (chip->i2c_ndx == 1)
  {
    DEBUGF("on_i2c_event: setting ctrl_reg only: %X\n", chip->ctrl_reg);
    return;
  }

  switch(chip->ctrl_reg) {
    case MODE1:
      on_mode1(chip);
      return;
    case MODE2:
      on_mode2(chip);
      return;
    case ALL_LED_ON_L:
      on_all_chan_pwm(chip);
      return;
    case PRE_SCALE:
      on_prescale(chip);
      return;      
  }
  if (chip->ctrl_reg >= LED_PWM_START && LED_PWM_END >= chip->ctrl_reg)
  {
    on_chan_pwm(chip);
    return;
  }

  DEBUGF("Unknown/unhandled i2c command, ignoring\n");
}

static void on_gen_call_cmd(chip_desc_t *chip)
{
  if (chip->i2c_ndx != 1)
  {
    DEBUGF("on_gen_call_cmd: expecing one data byte, but got: %X\n", chip->i2c_ndx);
    return;
  }

  if (chip->i2c_buffer[0] != ADDR_SOFTWARE_RESET)
  {
    DEBUGF("on_gen_call_cmd: expecing data byte to be reset, but got: %X\n", chip->i2c_buffer[0]);
    return;
  }

  chip_reset(chip);
}

static void on_mode1(chip_desc_t *chip)
{
  DEBUGF("on_mode1\n");

  if (chip->i2c_ndx != 2)
  { // one address byte and 1 data byte
    DEBUGF("Unexpected MODE1 command sequence - received %d bytes instead of 2, ignoring\n", chip->i2c_ndx);
    return;
  }

  uint8_t cmd = chip->i2c_buffer[1];

  if (cmd & MODE1_RESTART)
  {
    DEBUGF("on_mode1 - restart called\n");

    chip->mode1 &= ~(MODE1_RESTART|MODE1_SLEEP);
    sched_chan_timers(chip);
    // sched_timer(chip);
    return;
  }

  if (cmd & MODE1_SLEEP)
  {
    DEBUGF("on_mode1 - sleep called\n");
    // todo(bonnyr): delay setting RESTART bit until end of current cycle
    chip->mode1 = cmd | MODE1_SLEEP | MODE1_RESTART;

    timer_stop(chip->timer);
    for (int i = 0; i < MAX_CHANNELS; i++) { timer_stop(chip->channels[i].timer); }
    return;
  }

  if (cmd & MODE1_EXTCLK && !(cmd & MODE1_SLEEP))
  {
    DEBUGF("External clock not supported yet\n");
  }
}

static void on_mode2(chip_desc_t *chip)
{
  DEBUGF("MODE2 command not supported, ignoring\n");
}

static void on_prescale(chip_desc_t *chip)
{
  if (chip->i2c_ndx != 2)
  { // one address byte and 1 data byte
    DEBUGF("Unexpected PRESCALE command sequence - received %d bytes instead of 2, ignoring\n", chip->i2c_ndx);
    return;
  }

  set_prescale(chip, chip->i2c_buffer[1]);
  // sched_timer(chip);
}

static void on_chan_pwm(chip_desc_t *chip)
{
  uint8_t chan_ndx = (chip->i2c_buffer[0] - LED_PWM_START) / 4;
  chan_desc_t *chan = chip->channels + chan_ndx;

  if (chip->i2c_ndx != 5)
  { // one address byte and 4 data bytes
    DEBUGF("Unexpected PWM command sequence - received %d bytes instead of 5, ignoring\n", chip->i2c_ndx);
    return;
  }

  set_chan_pwm(chip, chan);
}

static void on_all_chan_pwm(chip_desc_t *chip)
{
  if (chip->i2c_ndx != 5)
  { // one address byte and 4 data bytes
    DEBUGF("Unexpected ALL PWM command sequence - received %d bytes instead of 5, ignoring\n", chip->i2c_ndx);
    return;
  }

  for (int i = 0; i < MAX_CHANNELS; i++)
  {
    set_chan_pwm(chip, chip->channels + i);
  }
}

static void on_timer_event(void *data)
{
  chip_desc_t *chip = data;
  uint64_t sim_micros = get_sim_nanos() / 1000;

  DEBUGF("%lld - timer expired, chip->next @%d\n", sim_micros, chip->next);
  uint16_t next = CHAN_TIMER_MAX_VALUE;

  for (int i = 0; i < MAX_CHANNELS; i++)
  {
    chan_desc_t *chan = chip->channels + i;

    if (chan->state != ST_PWM)
    {
      // DEBUGF("%lld chan %d is not PWM, ignoring\n", sim_micros, i);
      continue;
    }

    if (chip->next == chan->on_time)
    {
      chan->pin_level = HIGH;
      pin_write(chan->pin, chan->pin_level);
      DEBUGF("%lld setting %s pwm_on @%d\n", sim_micros, chan->name, chan->on_time);
    }
    else if (chip->next == chan->off_time)
    {
      chan->pin_level = LOW;
      pin_write(chan->pin, chan->pin_level);
      DEBUGF("%lld setting %s pwm_off @%d\n", sim_micros, chan->name, chan->off_time);
    };

    if (chan->on_time > chip->next && next > chan->on_time && chan->pin_level == LOW)
    {
      next = chan->on_time;
      DEBUGF("%lld setting next expiry for %s (%d) on time @%d\n", sim_micros, chan->name, chan->pin_level, next);
    }
    else if (chan->off_time > chip->next && next > chan->off_time && chan->pin_level == HIGH)
    {
      next = chan->off_time;
      DEBUGF("%lld setting next expiry for %s (%d) off time @%d\n", sim_micros, chan->name, chan->pin_level, next);
    }
  }

  uint16_t next_expiry = next - chip->next;
  if (next == CHAN_TIMER_MAX_VALUE) {
    // next_expiry = CHAN_TIMER_MAX_VALUE;
    next = 0;
  }
  DEBUGF("%lld arming pwm timer to expire in %dus, next_expiry %d, next %d, chip->next %d \n", 
    sim_micros, chip->duration_us * next_expiry, next_expiry, next, chip->next);
  timer_start(chip->timer, chip->duration_us * next_expiry, false);
  chip->next = next & CHAN_TIMER_MASK;
} 

static void on_chan_timer_event(void *data)
{
  chan_desc_t *chan = data;
  uint64_t sim_micros = get_sim_nanos() / 1000;

  DEBUGF("%lld - %s chan timer expired @%d\n", sim_micros, chan->name, chan->next);
  uint16_t next = CHAN_TIMER_MAX_VALUE;

  if (chan->state != ST_PWM)
  {
    DEBUGF("%lld - %s chan state is not PWM, not restarting\n", sim_micros, chan->name);
    return;
  }

  if (chan->next == chan->on_time)
  {
    DEBUGF("%lld setting %s chan pwm_on @%d\n", sim_micros, chan->name, chan->on_time);
    chan->pin_level = HIGH;
    pin_write(chan->pin, chan->pin_level);
    next = chan->off_time;
  }
  else if (chan->next == chan->off_time)
  {
    DEBUGF("%lld setting %s chan pwm_off @%d\n", sim_micros, chan->name, chan->off_time);
    chan->pin_level = LOW;
    pin_write(chan->pin, chan->pin_level);
    next = chan->on_time;
  } else if (chan->next == 0) {
    next = chan->on_time;
    DEBUGF("%lld setting %s chan to fire @%d\n", sim_micros, chan->name, chan->on_time);
  };

  if (next < chan->next) {
    next += CHAN_TIMER_MAX_VALUE;
  }

  DEBUGF("%lld arming %s chan pwm timer to expire in %d, next %d, chan->next %d \n", 
    sim_micros, chan->name, chan->chip->duration_us * (next - chan->next), next, chan->next);
  timer_start(chan->timer, chan->chip->duration_us * (next - chan->next), false);
  chan->next = next & CHAN_TIMER_MASK;
}

// --- Private helpers ---
static inline uint8_t addr_pins_to_addr(chip_desc_t *chip)
{
  uint8_t addr_val = 0;
  for (int a = 0; a < MAX_ADDR_PINS; a++)
  {
    addr_val |= pin_read(chip->addr_pins[a]) << a;
  }
  return addr_val;
}

// sched_timer stops the current timer and restarts it, setting the callback to the first active timer value
static void sched_timer(chip_desc_t *chip)
{
  timer_stop(chip->timer);

  uint16_t next = CHAN_TIMER_MAX_VALUE;
  for (int i = 0; i < MAX_CHANNELS; i++)
  {
    if (chip->channels[i].state == ST_PWM && chip->next > chip->channels[i].on_time)
    {
      next = chip->channels[i].on_time;
    };
  }

  DEBUGF("%lld starting timer, setting expiry to: %dus, next: %d\n", get_sim_nanos() / 1000, chip->duration_us * next, next);
  timer_start(chip->timer, chip->duration_us * next, false);
}


// sched_chan_timers stops the current timer and restarts it, setting the callback to the first active timer value
static void sched_chan_timers(chip_desc_t *chip)
{
  chip->last_start = get_sim_nanos();

  for (int i = 0; i < MAX_CHANNELS; i++)
  {
    chan_desc_t *chan = chip->channels + i;
    if (chan->state == ST_PWM )
    {
      DEBUGF("starting %s chan timer for: %d\n", chip->channels[i].name, chip->duration_us * chan->on_time);
      timer_start(chan->timer, chip->duration_us * chan->on_time, false);
    };
  }
}

static void set_prescale(chip_desc_t *chip, uint8_t prescale)
{
  if (!(chip->mode1 & MODE1_SLEEP))
  {
    printf("err: cannot set prescale whilst oscilator is running. Ignoring\n");
    return;
  }

  chip->prescale = prescale;
  chip->duration_us = MICROS_IN_SEC / (OSC_FREQ / (chip->prescale + 1));
  // for debugging timers, slow down the operation so a cycle takes ~1s
  if (debug_timer) {
    chip->duration_us = 256;
  }
  DEBUGF("setting prescale to %d, update rate: %d\n", chip->prescale, chip->duration_us);
}

static void set_chan_pwm(chip_desc_t *chip, chan_desc_t *chan)
{
    timer_stop(chan->timer);

  // check for always off indicator - this has precedence
  if (chip->i2c_buffer[4] & 0x10)
  {
    DEBUGF("setting channel %ld to always off\n", chan - chip->channels);
    chan->state = ST_PIN_OFF;
    chan->pin_level = LOW;
    pin_write(chan->pin, chan->pin_level);
    return;
  }

  // check for always on indicator
  if (chip->i2c_buffer[2] & 0x10)
  {
    DEBUGF("setting channel %ld to always on\n", chan - chip->channels);
    chan->state = ST_PIN_ON;
    chan->pin_level = HIGH;
    pin_write(chan->pin, chan->pin_level);
    return;
  }

  // we're either setting or remaining in PWM.
  chan->state = ST_PWM;
  chan->on_time = ((uint16_t)(chip->i2c_buffer[2] & 0xF) << 8 | (chip->i2c_buffer[1])) & CHAN_TIMER_MASK;
  chan->off_time = (((uint16_t)chip->i2c_buffer[4] & 0xF) << 8 | (chip->i2c_buffer[3])) & CHAN_TIMER_MASK;

  // if chip is not asleep, we need to start timer
  if (! (chip->mode1 & MODE1_SLEEP)) {
    // schedule for the next frame 
    uint64_t frame_dur = CHAN_TIMER_MAX_VALUE * chip->duration_us;
    uint64_t now = get_sim_nanos();
    uint64_t frame_off = ((now - chip->last_start) / 1000) % frame_dur;
   
    timer_start(chan->timer, frame_dur - frame_off, false);
    DEBUGF("%lld - Starting channel %s timer for %lldus\n", now / 1000, chan->name, frame_dur - frame_off);
  }
  DEBUGF("%lld - Setting channel %s - on:%d, off:%d\n", get_sim_nanos() / 1000, chan->name, chan->on_time, chan->off_time);
}