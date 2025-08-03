#include "coap-engine.h"
#include "os/dev/leds.h"
#include "sys/log.h"
#include <string.h>
#include <strings.h>

#define LOG_MODULE "res-cc-heater"
#define LOG_LEVEL LOG_LEVEL_INFO

int heater_on = 0;
extern int fan_on;
extern coap_resource_t res_cc_fan;

/* Forward declarations */
static void res_get_handler(coap_message_t *request, coap_message_t *response,
                            uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
static void res_put_handler(coap_message_t *request, coap_message_t *response,
                            uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
static void res_trigger_handler(void); // NEW

/* CoAP resource definition */
RESOURCE(res_cc_heater,
         "title=\"Heater actuator\";rt=\"Control\";obs",
         res_get_handler,
         NULL,
         res_put_handler,
         NULL);

/* Public init function to register .trigger */
void cc_heater_resource_init(void) {
  res_cc_heater.trigger = res_trigger_handler;
}

/* GET handler */
static void
res_get_handler(coap_message_t *request, coap_message_t *response,
                uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  const char *mode_str = heater_on ? "on" : "off";

  coap_set_header_content_format(response, APPLICATION_JSON);
  size_t len = snprintf((char *)buffer, COAP_MAX_CHUNK_SIZE,
                        "{\"mode\":\"%s\"}", mode_str);
  coap_set_payload(response, buffer, len);
}

/* PUT handler */
static void
res_put_handler(coap_message_t *request, coap_message_t *response,
                uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  const uint8_t *payload = NULL;
  size_t len = coap_get_payload(request, &payload);

  if(payload == NULL || len == 0) {
    LOG_INFO("empty payload\n");
    coap_set_status_code(response, BAD_REQUEST_4_00);
    return;
  }

  char command[8];
  if(len >= sizeof(command)) len = sizeof(command) - 1;
  memcpy(command, payload, len);
  command[len] = '\0';

  if(strcasecmp(command, "on") == 0) {
    heater_on = 1;
    leds_single_on(LEDS_YELLOW);
    fan_on = 0; // spegne il fan
    coap_notify_observers(&res_cc_fan);
    LOG_INFO("heater turned on\n");
  } else if(strcasecmp(command, "off") == 0) {
    heater_on = 0;
    leds_single_off(LEDS_YELLOW);
    LOG_INFO("heater turned off\n");
  } else {
    LOG_INFO("unknown mode: %s\n", command);
    coap_set_status_code(response, BAD_REQUEST_4_00);
    return;
  }

  coap_set_status_code(response, CHANGED_2_04);
  coap_notify_observers(&res_cc_heater);
}

/* TRIGGER handler */
static void
res_trigger_handler(void)
{
  LOG_INFO("Triggering heater toggle\n");
  heater_on = !heater_on;

  if(heater_on) {
    leds_single_on(LEDS_YELLOW);
    LOG_INFO("heater turned ON (via trigger)\n");
  } else {
    leds_single_off(LEDS_YELLOW);
    LOG_INFO("heater turned OFF (via trigger)\n");
  }

  coap_notify_observers(&res_cc_heater);
}

