#include "wokwi-api.h"
#include <stdlib.h>
#include <stdint.h>

typedef struct {
  pin_t pin_out;
  uint32_t attr_flow_rate;
  timer_t timer;
} chip_state_t;

static void update_sensor_output(void *user_data) {
  chip_state_t *chip = (chip_state_t *)user_data;

  float flow_rate = attr_read_float(chip->attr_flow_rate);

  if (flow_rate < 0.0f) {
    flow_rate = 0.0f;
  }

  if (flow_rate > 30.0f) {
    flow_rate = 30.0f;
  }

  /*
   * Simulación:
   * 0 L/min  -> 0.0 V
   * 30 L/min -> 3.3 V
   */
  float voltage = (flow_rate / 30.0f) * 3.3f;

  pin_dac_write(chip->pin_out, voltage);
}

void chip_init(void) {
  chip_state_t *chip = (chip_state_t *)calloc(1, sizeof(chip_state_t));

  chip->pin_out = pin_init("OUT", ANALOG);
  chip->attr_flow_rate = attr_init_float("flowRate", 8.0f);

  const timer_config_t timer_config = {
    .callback = update_sensor_output,
    .user_data = chip
  };

  chip->timer = timer_init(&timer_config);
  timer_start(chip->timer, 100000, true);
}   