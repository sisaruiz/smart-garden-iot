#include "contiki.h"
#include "coap-engine.h"
#include "os/dev/leds.h"
#include "os/dev/button-hal.h"
#include "sys/etimer.h"
#include "sys/log.h"
#include <string.h>
#include <strings.h>

#define LOG_MODULE "grow_light_res"
#define LOG_LEVEL LOG_LEVEL_INFO

/* Enum for grow light modes */
static enum {
  MODE_OFF,
  MODE_ON,
  MODE_ALERT
} current_mode = MODE_OFF;

static struct etimer button_timer;
static button_hal_button_t *btn;

/* Forward declarations */
static void res_get_handler(coap_message_t *request,
                            coap_message_t *response,
                            uint8_t *buffer,
                            uint16_t preferred_size,
                            int32_t *offset);
static void res_put_handler(coap_message_t *request,
                            coap_message_t *response,
                            uint8_t *buffer,
                            uint16_t preferred_size,
                            int32_t *offset);
static void res_trigger_handler(void);

PROCESS(grow_light_button_process, "Grow Light Button Handler");

/* CoAP resource definition */
RESOURCE(res_grow_light,
         "title=\"Grow Light\";rt=\"Control\";obs",
         res_get_handler,
         NULL,
         res_put_handler,
         NULL);

/*---------------------------------------------------------------------------*/
/* Public init function for use in coap-device.c */
void grow_light_resource_init(void) {
  res_grow_light.trigger = res_trigger_handler;
}

/*---------------------------------------------------------------------------*/
static void
res_get_handler(coap_message_t *request,
                coap_message_t *response,
                uint8_t *buffer,
                uint16_t preferred_size,
                int32_t *offset)
{
  const char *mode_str =
    (current_mode == MODE_ON)    ? "on" :
    (current_mode == MODE_ALERT) ? "alert" : "off";

  coap_set_header_content_format(response, APPLICATION_JSON);
  size_t len = snprintf((char *)buffer, COAP_MAX_CHUNK_SIZE,
                        "{\"mode\":\"%s\"}", mode_str);
  coap_set_payload(response, buffer, len);
}

/*---------------------------------------------------------------------------*/
static void
res_put_handler(coap_message_t *request,
                coap_message_t *response,
                uint8_t *buffer,
                uint16_t preferred_size,
                int32_t *offset)
{
  const uint8_t *payload = NULL;
  size_t len = coap_get_payload(request, &payload);
  int success = 1;

  LOG_INFO("Grow Light PUT received\n");

  if(len == 0 || payload == NULL) {
    coap_set_status_code(response, BAD_REQUEST_4_00);
    return;
  }

  char mode[16];
  if(len >= sizeof(mode)) len = sizeof(mode) - 1;
  memcpy(mode, payload, len);
  mode[len] = '\0';

  if(strcmp(mode, "on") == 0) {
    current_mode = MODE_ON;
    leds_single_on(LEDS_BLUE);
    leds_off(LEDS_RED);
    LOG_INFO("Mode set to on\n");
  } else if(strcmp(mode, "off") == 0) {
    current_mode = MODE_OFF;
    leds_off(LEDS_BLUE | LEDS_RED);
    LOG_INFO("Mode set to off\n");
  } else if(strcmp(mode, "alert") == 0) {
    current_mode = MODE_ALERT;
    leds_single_on(LEDS_RED);
    leds_off(LEDS_BLUE);
    LOG_INFO("Mode set to alert\n");
    process_start(&grow_light_button_process, NULL);
  } else {
    LOG_WARN("Unknown mode: %s\n", mode);
    success = 0;
  }

  if(!success) {
    coap_set_status_code(response, BAD_REQUEST_4_00);
    return;
  }

  coap_notify_observers(&res_grow_light);
  res_get_handler(request, response, buffer, preferred_size, offset);
}

/*---------------------------------------------------------------------------*/
static void
res_trigger_handler(void)
{
  LOG_INFO("Triggering grow light toggle\n");

  if(current_mode == MODE_ALERT) {
    LOG_INFO("Ignoring trigger while in ALERT mode\n");
    return;
  }

  current_mode = (current_mode == MODE_ON) ? MODE_OFF : MODE_ON;

  if(current_mode == MODE_ON) {
    leds_single_on(LEDS_BLUE);
    leds_off(LEDS_RED);
    LOG_INFO("Grow light triggered ON\n");
  } else {
    leds_off(LEDS_BLUE | LEDS_RED);
    LOG_INFO("Grow light triggered OFF\n");
  }

  coap_notify_observers(&res_grow_light);
}

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(grow_light_button_process, ev, data)
{
  PROCESS_BEGIN();

  btn = button_hal_get_by_index(0);

  while(current_mode == MODE_ALERT) {
    PROCESS_YIELD();

    if(ev == button_hal_press_event) {
      LOG_INFO("Button pressed during alert. Waiting 5s hold...\n");
      etimer_set(&button_timer, CLOCK_SECOND * 5);
      PROCESS_WAIT_EVENT_UNTIL(ev == button_hal_release_event || etimer_expired(&button_timer));

      if(etimer_expired(&button_timer)) {
        LOG_INFO("Alert acknowledged. Blinking LED RED...\n");

        for(int i = 0; i < 10; i++) {
          leds_toggle(LEDS_RED);
          clock_wait(CLOCK_SECOND / 2);
        }

        leds_off(LEDS_RED);
        current_mode = MODE_OFF;
        LOG_INFO("Alert cleared. Grow light set to off.\n");

        coap_notify_observers(&res_grow_light);
      } else {
        LOG_INFO("Button released too soon. Alert not cleared.\n");
      }
    }
  }

  PROCESS_END();
}

