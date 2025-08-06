#include "contiki.h"
#include "coap-engine.h"
#include "os/dev/leds.h"
#include "sys/log.h"
#include <string.h>
#include <strings.h>
#include <stdbool.h> 

#define LOG_MODULE "fertilizer_res"
#define LOG_LEVEL LOG_LEVEL_INFO

/* Exposed by coap-device.c (must be non-static there) */
extern bool fertilizer_needs_refill;

/* How many OFF -> ON transitions before the tank is empty */
#ifndef MAX_FERTILIZER_USES
#define MAX_FERTILIZER_USES 3
#endif

/* Local usage counter (counts OFF->ON transitions only) */
static int fertilizer_use_count = 0;

enum FertilizerMode {
  MODE_OFF,
  MODE_ACIDIC,
  MODE_ALKALINE
};

static enum FertilizerMode current_mode = MODE_OFF;

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

  /* Block usage if tank is empty (manual refill required) */
  if(fertilizer_needs_refill) {
    LOG_WARN("Fertilizer empty: manual refill required\n");
    coap_set_status_code(response, SERVICE_UNAVAILABLE_5_03);
    return;
  }

  if(len == 0 || payload == NULL) {
    coap_set_status_code(response, BAD_REQUEST_4_00);
    return;
  }

  /* Parse requested mode */
  char mode[16];
  if(len >= sizeof(mode)) len = sizeof(mode) - 1;
  memcpy(mode, payload, len);
  mode[len] = '\0';

  enum FertilizerMode requested = current_mode;
  int known = 1;

  if(strcasecmp(mode, "sinc") == 0) {
    requested = MODE_ACIDIC;
  } else if(strcasecmp(mode, "sdec") == 0) {
    requested = MODE_ALKALINE;
  } else if(strcasecmp(mode, "off") == 0) {
    requested = MODE_OFF;
  } else {
    known = 0;
  }

  if(!known) {
    LOG_WARN("Unknown mode: %s\n", mode);
    coap_set_status_code(response, BAD_REQUEST_4_00);
    return;
  }

  enum FertilizerMode prev = current_mode;

  /* LEDs for visual hint */
  if(requested == MODE_ACIDIC) {
    leds_single_on(LEDS_GREEN);
    leds_single_off(LEDS_BLUE);
  } else if(requested == MODE_ALKALINE) {
    leds_single_on(LEDS_BLUE);
    leds_single_off(LEDS_GREEN);
  } else { /* MODE_OFF */
    leds_off(LEDS_GREEN | LEDS_BLUE);
  }

  /* Count ONLY OFF -> (ACIDIC|ALKALINE) transitions */
  if(prev == MODE_OFF && (requested == MODE_ACIDIC || requested == MODE_ALKALINE)) {
    fertilizer_use_count++;
    LOG_INFO("Dispense cycle started: %d/%d\n", fertilizer_use_count, MAX_FERTILIZER_USES);

    if(fertilizer_use_count >= MAX_FERTILIZER_USES) {
      /* Depleted now: require manual refill, force OFF, show red */
      fertilizer_needs_refill = true;
      fertilizer_use_count = 0;
      current_mode = MODE_OFF;
      leds_on(LEDS_RED);  // steady red until manual refill
      LOG_WARN("Fertilizer depleted -> needs refill (forcing OFF)\n");

      coap_notify_observers(&res_fertilizer);
      res_get_handler(request, response, buffer, preferred_size, offset);
      return;
    }
  }

  /* Commit requested mode (not depleted) */
  current_mode = requested;

  coap_notify_observers(&res_fertilizer);
  res_get_handler(request, response, buffer, preferred_size, offset);
}

/*---------------------------------------------------------------------------*/
/* Triggered by button press (manual refill confirmation) */
static void
res_trigger_handler(void)
{
  LOG_INFO("Fertilizer refill confirmed (trigger)\n");

  /* Clear empty state; keep counter at current value (was reset on depletion) */
  fertilizer_needs_refill = false;
  leds_off(LEDS_RED);  // red off after refill

  /* Notify observers (even if mode unchanged) */
  coap_notify_observers(&res_fertilizer);
}

