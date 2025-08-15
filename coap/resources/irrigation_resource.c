#include "contiki.h"
#include "coap-engine.h"
#include "os/dev/leds.h"
#include "sys/log.h"
#include <string.h>
#include <strings.h>

#define LOG_MODULE "irrigation_res"
#define LOG_LEVEL LOG_LEVEL_INFO

static enum {
  IRRIGATION_OFF,
  IRRIGATION_ON,
} irrigation_mode = IRRIGATION_OFF;


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
     (irrigation_mode == IRRIGATION_ON) ? "on" : "off";

  coap_set_header_content_format(response, APPLICATION_JSON);
  size_t len = snprintf((char *)buffer, COAP_MAX_CHUNK_SIZE,
                        "{\"mode\":\"%s\"}", mode_str);
  coap_set_payload(response, buffer, len);
}


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

static void
res_trigger_handler(void)
{
  LOG_INFO("Triggering irrigation toggle\n");

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


