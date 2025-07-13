#include "contiki.h"
#include "coap-engine.h"
#include "os/dev/leds.h"
#include "sys/log.h"
#include <string.h>
#include <strings.h>

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
static void res_trigger_handler(void);

/* CoAP resource definition (standard 6 arguments) */
RESOURCE(res_fertilizer,
         "title=\"Fertilizer Dispenser\";rt=\"Control\";obs",
         res_get_handler,
         NULL,
         res_put_handler,
         NULL);

/* Public init function to assign trigger handler */
void fertilizer_resource_init(void) {
  res_fertilizer.trigger = res_trigger_handler;
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
  const uint8_t *payload = NULL;
  size_t len = coap_get_payload(request, &payload);

  LOG_INFO("Fertilizer PUT received\n");

  if(len == 0 || payload == NULL) {
    coap_set_status_code(response, BAD_REQUEST_4_00);
    return;
  }

  char mode[16];
  if(len >= sizeof(mode)) len = sizeof(mode) - 1;
  memcpy(mode, payload, len);
  mode[len] = '\0';  // Null-terminate

  int success = 1;

  if(strcasecmp(mode, "sinc") == 0) {
    current_mode = MODE_ACIDIC;
    LOG_INFO("Mode set to acidic\n");
    leds_single_on(LEDS_GREEN);
  } else if(strcasecmp(mode, "sdec") == 0) {
	  current_mode = MODE_ALKALINE;
	  LOG_INFO("Mode set to alkaline\n");
	  leds_single_on(LEDS_BLUE);
  } else if(strcasecmp(mode, "off") == 0) {
	  current_mode = MODE_OFF;
	  LOG_INFO("Mode set to off\n");
	  leds_off(LEDS_GREEN | LEDS_BLUE);
  }
  
  if(!success) {
    coap_set_status_code(response, BAD_REQUEST_4_00);
    return;
  }

  coap_notify_observers(&res_fertilizer);
  res_get_handler(request, response, buffer, preferred_size, offset);
}

/*---------------------------------------------------------------------------*/
/* Triggered by button press (manual refill simulation) */
static void
res_trigger_handler(void)
{
  LOG_INFO("Fertilizer manually dispensed (trigger)\n");

  // Simulate dispensing: flash LED to indicate action
  leds_on(LEDS_RED);
  clock_wait(CLOCK_SECOND / 2);
  leds_off(LEDS_RED);

  // Notify observers (even if state hasn't changed)
  coap_notify_observers(&res_fertilizer);
}

