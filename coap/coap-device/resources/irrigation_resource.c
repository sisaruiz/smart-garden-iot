#include "contiki.h"
#include "coap-engine.h"
#include "os/dev/leds.h"
#include "os/dev/button-hal.h"
#include "sys/etimer.h"
#include "sys/log.h"
#include <string.h>
#include <strings.h>

#define LOG_MODULE "irrigation_res"
#define LOG_LEVEL LOG_LEVEL_INFO

static enum {
  IRRIGATION_OFF,
  IRRIGATION_ON,
  IRRIGATION_ALERT
} irrigation_mode = IRRIGATION_OFF;

static struct etimer button_timer;
static button_hal_button_t *btn;

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
static void res_trigger_handler(void); // NEW

PROCESS(irrigation_button_process, "Irrigation Button Handler");

/* Define resource */
RESOURCE(res_irrigation,
         "title=\"Irrigation\";rt=\"Control\";obs",
         res_get_handler,
         NULL,
         res_put_handler,
         NULL);

/* Public init function for coap-device.c */
void irrigation_resource_init(void) {
  res_irrigation.trigger = res_trigger_handler;
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
    (irrigation_mode == IRRIGATION_ON)    ? "on" :
    (irrigation_mode == IRRIGATION_ALERT) ? "alert" : "off";

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

  LOG_INFO("Irrigation PUT received\n");

  if(len == 0 || payload == NULL) {
    coap_set_status_code(response, BAD_REQUEST_4_00);
    return;
  }

  char mode[16];
  if(len >= sizeof(mode)) len = sizeof(mode) - 1;
  memcpy(mode, payload, len);
  mode[len] = '\0';

  if(strcasecmp(mode, "on") == 0) {
    irrigation_mode = IRRIGATION_ON;
    leds_single_off(LEDS_RED);
    LOG_INFO("Mode set to on\n");
  } else if(strcasecmp(mode, "off") == 0) {
    irrigation_mode = IRRIGATION_OFF;
    leds_single_off(LEDS_RED);
    LOG_INFO("Mode set to off\n");
  } else if(strcasecmp(mode, "alert") == 0) {
    irrigation_mode = IRRIGATION_ALERT;
    leds_single_on(LEDS_RED);
    LOG_INFO("Mode set to alert (LED RED ON)\n");
    process_start(&irrigation_button_process, NULL);
  } else {
    LOG_WARN("Unknown mode: %s\n", mode);
    success = 0;
  }

  if(!success) {
    coap_set_status_code(response, BAD_REQUEST_4_00);
    return;
  }

  coap_notify_observers(&res_irrigation);
  res_get_handler(request, response, buffer, preferred_size, offset);
}

/*---------------------------------------------------------------------------*/
static void
res_trigger_handler(void)
{
  LOG_INFO("Triggering irrigation toggle\n");

  if(irrigation_mode == IRRIGATION_ALERT) {
    LOG_INFO("Ignoring trigger while in ALERT mode\n");
    return;
  }

  irrigation_mode = (irrigation_mode == IRRIGATION_ON) ? IRRIGATION_OFF : IRRIGATION_ON;

  if(irrigation_mode == IRRIGATION_ON) {
    leds_single_off(LEDS_RED);
    LOG_INFO("Irrigation triggered ON\n");
  } else {
    leds_single_off(LEDS_RED);
    LOG_INFO("Irrigation triggered OFF\n");
  }

  coap_notify_observers(&res_irrigation);
}

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(irrigation_button_process, ev, data)
{
  PROCESS_BEGIN();

  btn = button_hal_get_by_index(0);

  while(irrigation_mode == IRRIGATION_ALERT) {
    PROCESS_YIELD();

    if(ev == button_hal_press_event) {
      LOG_INFO("Button pressed. Waiting for 5s hold...\n");
      etimer_set(&button_timer, CLOCK_SECOND * 5);
      PROCESS_WAIT_EVENT_UNTIL(ev == button_hal_release_event || etimer_expired(&button_timer));

      if(etimer_expired(&button_timer)) {
        LOG_INFO("Alert acknowledged. Blinking LED RED...\n");

        for(int i = 0; i < 10; i++) {
          leds_toggle(LEDS_RED);
          clock_wait(CLOCK_SECOND / 2);
        }

        leds_single_off(LEDS_RED);
        irrigation_mode = IRRIGATION_OFF;
        LOG_INFO("Alert cleared. Irrigation set to off.\n");

        coap_notify_observers(&res_irrigation);
      } else {
        LOG_INFO("Button released too soon. Alert not cleared.\n");
      }
    }
  }

  PROCESS_END();
}

