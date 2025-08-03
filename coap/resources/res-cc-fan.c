#include "coap-engine.h"
#include "os/dev/leds.h"
#include "sys/log.h"
#include <string.h>
#include <strings.h>

#define LOG_MODULE "res-cc-fan"
#define LOG_LEVEL LOG_LEVEL_INFO

int fan_on = 0;
extern int heater_on;
extern coap_resource_t res_cc_heater;

/* Forward declarations */
static void res_get_handler(coap_message_t *request, coap_message_t *response,
                            uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
static void res_put_handler(coap_message_t *request, coap_message_t *response,
                            uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
static void res_trigger_handler(void); //

/* CoAP resource definition */
RESOURCE(res_cc_fan,
         "title=\"Fan actuator\";rt=\"Control\";obs",
         res_get_handler,
         NULL,
         res_put_handler,
         NULL);

/* Public init function to register .trigger */
void cc_fan_resource_init(void) {
  res_cc_fan.trigger = res_trigger_handler;
}

/* GET handler */
static void
res_get_handler(coap_message_t *request, coap_message_t *response,
                uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  const char *mode_str = fan_on ? "on" : "off";

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
    fan_on = 1;
    leds_single_on(LEDS_GREEN);
    heater_on = 0; 
    coap_notify_observers(&res_cc_heater);
    LOG_INFO("fan turned on\n");
  } else if(strcasecmp(command, "off") == 0) {
    fan_on = 0;
    leds_single_off(LEDS_GREEN);
    LOG_INFO("fan turned off\n");
  } else {
    LOG_INFO("unknown mode: %s\n", command);
    coap_set_status_code(response, BAD_REQUEST_4_00);
    return;
  }

  coap_set_status_code(response, CHANGED_2_04);
  coap_notify_observers(&res_cc_fan);
}

/* TRIGGER handler */
static void
res_trigger_handler(void)
{
  LOG_INFO("Triggering fan toggle\n");
  fan_on = !fan_on;

  if(fan_on) {
    leds_single_on(LEDS_GREEN);
    LOG_INFO("fan turned ON (via trigger)\n");
  } else {
    leds_single_off(LEDS_GREEN);
    LOG_INFO("fan turned OFF (via trigger)\n");
  }

  coap_notify_observers(&res_cc_fan);
}

