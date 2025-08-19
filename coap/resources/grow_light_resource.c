#include "contiki.h"
#include "coap-engine.h"
#include "os/dev/leds.h"
#include "sys/log.h"
#include <string.h>
#include <strings.h>

#define LOG_MODULE "grow_light_res"
#define LOG_LEVEL LOG_LEVEL_INFO

/* Modes */
static enum {
  MODE_OFF,
  MODE_ON
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

/* CoAP resource */
RESOURCE(res_grow_light,
         "title=\"Grow Light\";rt=\"Control\";obs",
         res_get_handler,
         NULL,
         res_put_handler,
         NULL);

/* Public init for coap-device.c */
void grow_light_resource_init(void) {
  res_grow_light.trigger = res_trigger_handler;
}

/* ----------------- Handlers ----------------- */
static void
res_get_handler(coap_message_t *request,
                coap_message_t *response,
                uint8_t *buffer,
                uint16_t preferred_size,
                int32_t *offset)
{
  const char *mode_str = (current_mode == MODE_ON) ? "on" : "off";

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
    LOG_INFO("Mode set to on\n");

  } else if(strcmp(mode, "off") == 0) {
    current_mode = MODE_OFF;
    LOG_INFO("Mode set to off\n");

  } else {
    LOG_WARN("Unknown mode: %s\n", mode);
    coap_set_status_code(response, BAD_REQUEST_4_00);
    return;
  }

  coap_notify_observers(&res_grow_light);
  res_get_handler(request, response, buffer, preferred_size, offset);
}

static void
res_trigger_handler(void)
{
  LOG_INFO("Triggering grow light toggle\n");

  current_mode = (current_mode == MODE_ON) ? MODE_OFF : MODE_ON;

  if(current_mode == MODE_ON) {
    LOG_INFO("Grow light triggered ON\n");
  } else {
    LOG_INFO("Grow light triggered OFF\n");
  }

  coap_notify_observers(&res_grow_light);
}

