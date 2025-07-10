#include "contiki.h"
#include "coap-engine.h"
#include "os/dev/leds.h"
#include "sys/log.h"
#include <string.h>

#define LOG_MODULE "fertilizer_res"
#define LOG_LEVEL LOG_LEVEL_INFO

/* Enum for fertilizer modes */
static enum {
  MODE_OFF,
  MODE_ACIDIC,
  MODE_ALKALINE
} current_mode = MODE_OFF;

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

/* CoAP resource definition */
RESOURCE(res_fertilizer,
         "title=\"Fertilizer Dispenser\";rt=\"Control\";obs",
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
    (current_mode == MODE_ACIDIC)   ? "acidic" :
    (current_mode == MODE_ALKALINE) ? "alkaline" : "off";

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
  const char *mode = NULL;
  size_t len = coap_get_post_variable(request, "mode", &mode);
  int success = 1;

  LOG_INFO("Fertilizer PUT received\n");

  if(len) {
    if(strncmp(mode, "acidic", len) == 0) {
      current_mode = MODE_ACIDIC;
      LOG_INFO("Mode set to ACIDIC\n");
      leds_single_on(LEDS_GREEN);
    } else if(strncmp(mode, "alkaline", len) == 0) {
      current_mode = MODE_ALKALINE;
      LOG_INFO("Mode set to ALKALINE\n");
      leds_single_on(LEDS_BLUE);
    } else if(strncmp(mode, "off", len) == 0) {
      current_mode = MODE_OFF;
      LOG_INFO("Mode set to OFF\n");
      leds_off(LEDS_GREEN | LEDS_BLUE);
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

  // Notifica gli osservatori del cambiamento di stato
  coap_notify_observers(&res_fertilizer);

  // Echo the new state in the response
  res_get_handler(request, response, buffer, preferred_size, offset);
}
