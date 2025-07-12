#include "contiki.h"
#include "coap-engine.h"
#include "os/dev/leds.h"
#include "os/dev/button-hal.h"
#include "sys/etimer.h"
#include "sys/log.h"
#include <string.h>

#define LOG_MODULE "irrigation_res"
#define LOG_LEVEL LOG_LEVEL_INFO

// Modalità disponibili
static enum {
  IRRIGATION_OFF,
  IRRIGATION_ON,
  IRRIGATION_ALERT
} irrigation_mode = IRRIGATION_OFF;

// Timer per bottone
static struct etimer button_timer;
static button_hal_button_t *btn;  // ✅ Correct type

// Forward declarations
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

PROCESS(irrigation_button_process, "Irrigation button handler");

// CoAP resource definition
RESOURCE(res_irrigation,
         "title=\"Irrigation\";rt=\"Control\";obs",
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
    (irrigation_mode == IRRIGATION_ON)    ? "on" :
    (irrigation_mode == IRRIGATION_ALERT) ? "alert" : "off";

  coap_set_header_content_format(response, APPLICATION_JSON);
  size_t len = snprintf((char *)buffer, COAP_MAX_CHUNK_SIZE,
                        "{\"irrigation\":\"%s\"}", mode_str);
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

  LOG_INFO("Irrigation PUT received\n");

  if(len) {
    if(strncmp(mode, "on", len) == 0) {
      irrigation_mode = IRRIGATION_ON;
      leds_single_off(LEDS_RED);
      LOG_INFO("Mode set to ON\n");
    } else if(strncmp(mode, "off", len) == 0) {
      irrigation_mode = IRRIGATION_OFF;
      leds_single_off(LEDS_RED);
      LOG_INFO("Mode set to OFF\n");
    } else if(strncmp(mode, "alert", len) == 0) {
      irrigation_mode = IRRIGATION_ALERT;
      leds_single_on(LEDS_RED);
      LOG_INFO("Mode set to ALERT (LED RED ON)\n");
      process_start(&irrigation_button_process, NULL); // Attiva gestione bottone
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

  // Notifica osservatori
  coap_notify_observers(&res_irrigation);

  // Echo risposta
  res_get_handler(request, response, buffer, preferred_size, offset);
}

/*---------------------------------------------------------------------------*/
// Processo che gestisce il bottone per risolvere lo stato di allerta
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

        // Blink red LED 5 times
        for(int i = 0; i < 10; i++) {
          leds_toggle(LEDS_RED);
          clock_wait(CLOCK_SECOND / 2);
        }

        leds_single_off(LEDS_RED);
        irrigation_mode = IRRIGATION_OFF;

        LOG_INFO("Alert cleared. Irrigation set to OFF.\n");

        // Notifica osservatori
        coap_notify_observers(&res_irrigation);
      } else {
        LOG_INFO("Button released too soon. No action taken.\n");
      }
    }
  }

  PROCESS_END();
}

