#include "contiki.h"
#include "coap-engine.h"
#include "os/dev/leds.h"
#include "os/dev/button-hal.h"
#include "sys/etimer.h"
#include "sys/log.h"
#include <string.h>

#define LOG_MODULE "grow_light_res"
#define LOG_LEVEL LOG_LEVEL_INFO

/* Enum for grow light modes */
static enum {
  MODE_OFF,
  MODE_ON,
  MODE_ALERT
} current_mode = MODE_OFF;

/* Timers and button */
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

PROCESS(grow_light_button_process, "Grow Light Button Handler");

/* CoAP resource definition */
RESOURCE(res_grow_light,
         "title=\"Grow Light\";rt=\"Control\";obs",
         res_get_handler,
         NULL,
         res_put_handler,
         NULL);

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
                        "{\"grow_light\":\"%s\"}", mode_str);
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
  const char *mode = NULL;
  size_t len = coap_get_post_variable(request, "mode", &mode);
  int success = 1;

  LOG_INFO("Grow Light PUT received\n");

  if(len) {
    if(strncmp(mode, "on", len) == 0) {
      current_mode = MODE_ON;
      leds_single_on(LEDS_BLUE);
      leds_off(LEDS_RED);
      LOG_INFO("Mode set to ON\n");
    } else if(strncmp(mode, "off", len) == 0) {
      current_mode = MODE_OFF;
      leds_off(LEDS_BLUE | LEDS_RED);
      LOG_INFO("Mode set to OFF\n");
    } else if(strncmp(mode, "alert", len) == 0) {
      current_mode = MODE_ALERT;
      leds_single_on(LEDS_RED);
      leds_off(LEDS_BLUE);
      LOG_INFO("Mode set to ALERT\n");
      process_start(&grow_light_button_process, NULL); // Start alert handler
    } else {
      success = 0;
    }
  } else {
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
PROCESS_THREAD(grow_light_button_process, ev, data)
{
  PROCESS_BEGIN();

  btn = button_hal_get_by_index(0);

  while(current_mode == MODE_ALERT) {
    PROCESS_YIELD();

    if(ev == button_hal_press_event) {
      LOG_INFO("Button pressed during ALERT. Waiting 5s hold...\n");
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
        LOG_INFO("Alert cleared. Grow light set to OFF.\n");

        coap_notify_observers(&res_grow_light);
      } else {
        LOG_INFO("Button released too soon. Alert not cleared.\n");
      }
    }
  }

  PROCESS_END();
}

