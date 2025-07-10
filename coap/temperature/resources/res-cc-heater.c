#include "coap-engine.h"
#include "os/dev/leds.h"
#include "sys/log.h"

#define LOG_MODULE "res-cc-heater"
#define LOG_LEVEL LOG_LEVEL_INFO

static int heater_on = 0;

static void res_get_handler(coap_message_t *request, coap_message_t *response,
                            uint8_t *buffer, uint16_t preferred_size, int32_t *offset) {
  const char *msg = heater_on ? "heater=ON" : "heater=OFF";
  memcpy(buffer, msg, strlen(msg));
  coap_set_payload(response, buffer, strlen(msg));
}

static void res_put_handler(coap_message_t *request, coap_message_t *response,
                            uint8_t *buffer, uint16_t preferred_size, int32_t *offset) {
  size_t len = coap_get_payload(request, (const uint8_t **)&buffer);

  if(len == 2 && strncmp((char *)buffer, "on", 2) == 0) {
    heater_on = 1;
    leds_single_on(LEDS_YELLOW);
    LOG_INFO("Heater turned ON\n");
  } else if(len == 3 && strncmp((char *)buffer, "off", 3) == 0) {
    heater_on = 0;
    leds_single_off(LEDS_YELLOW);
    LOG_INFO("Heater turned OFF\n");
  } else {
    LOG_INFO("Unknown command\n");
    coap_set_status_code(response, BAD_REQUEST_4_00);
    return;
  }

  coap_set_status_code(response, CHANGED_2_04);
  coap_notify_observers(&res_cc_heater);
}

RESOURCE(res_cc_heater,
         "title=\"Heater actuator\";rt=\"Control\";obs",
         res_get_handler,
         res_put_handler,
         NULL,
         NULL);
