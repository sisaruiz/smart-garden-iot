#include "contiki.h"
#include "coap-engine.h"
#include "os/dev/leds.h"
#include "sys/log.h"
#include <string.h>
#include <strings.h>
#include <stdbool.h> 

#define LOG_MODULE "fertilizer_res"
#define LOG_LEVEL LOG_LEVEL_INFO

extern bool fertilizer_needs_refill;

/* OFF -> ON transitions before the tank is empty */
#ifndef MAX_FERTILIZER_USES 
#define MAX_FERTILIZER_USES 3 
#endif

/* usage counter */
static int fertilizer_use_count = 0;

enum FertilizerMode {
  MODE_OFF,
  MODE_ACIDIC,
  MODE_ALKALINE
};

static enum FertilizerMode current_mode = MODE_OFF;

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

RESOURCE(res_fertilizer,
         "title=\"Fertilizer Dispenser\";rt=\"Control\";obs",
         res_get_handler,
         NULL,
         res_put_handler,
         NULL);

/* function to assign trigger handler */
void fertilizer_resource_init(void) {
  res_fertilizer.trigger = res_trigger_handler;
}

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

static void
res_put_handler(coap_message_t *request,
                coap_message_t *response,
                uint8_t *buffer,
                uint16_t preferred_size,
                int32_t *offset)
{
  const uint8_t *payload = NULL;
  size_t len = coap_get_payload(request, &payload);

  /* DBG: log payload grezzo */
  LOG_INFO("Fertilizer PUT len=%d payload='%.*s'\n",
           (int)len, (int)len, (const char*)payload);

  /* Parse requested mode */
  char mode[16];
  if(len >= sizeof(mode)) len = sizeof(mode) - 1;
  memcpy(mode, payload, len);
  mode[len] = '\0';

  /* DBG: stato precedente + testo parsato */
  LOG_INFO("DBG prev_mode=%d  parsed_text='%s'\n", (int)current_mode, mode);

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

  /* DBG: esito parsing */
  LOG_INFO("DBG requested_enum=%d  known=%d\n", (int)requested, known);

  if(!known) {
    LOG_WARN("Unknown mode: %s\n", mode);
    coap_set_status_code(response, BAD_REQUEST_4_00);
    return;
  }

  enum FertilizerMode prev = current_mode;

  /* Count ONLY OFF -> (ACIDIC|ALKALINE) transitions */
  if(prev == MODE_OFF && (requested == MODE_ACIDIC || requested == MODE_ALKALINE)) {
    fertilizer_use_count++;
    LOG_INFO("Dispense cycle started: %d/%d\n", fertilizer_use_count, MAX_FERTILIZER_USES);

    if(fertilizer_use_count >= MAX_FERTILIZER_USES) {
      /* require manual refill, force OFF, show red */
      fertilizer_needs_refill = true;
      fertilizer_use_count = 0;
      current_mode = MODE_OFF;
      leds_off(LEDS_RED | LEDS_GREEN | LEDS_BLUE);
      leds_on(LEDS_RED);
      LOG_WARN("Fertilizer empty -> needs refill (forcing OFF)\n");

      /* DBG: maschera LED in depletion */
      {
        int mask = leds_get();
        LOG_INFO("DBG depletion_leds_mask=0x%02x\n", mask);
      }

      coap_notify_observers(&res_fertilizer);
      res_get_handler(request, response, buffer, preferred_size, offset);
      return;
    }
  }

  /* Only set current_mode to the newly requested mode if the fertilizer tank is not empty. */
  current_mode = requested;
  
  /* set LED based on MODE */
  leds_off(LEDS_RED | LEDS_GREEN | LEDS_BLUE);
  switch (current_mode) {
     case MODE_ACIDIC:   leds_on(LEDS_GREEN); break;   // sinc → greem
     case MODE_ALKALINE: leds_on(LEDS_BLUE);  break;   // sdec → blue
     case MODE_OFF: default:
       break;
  }

  /* DBG: stato finale + maschera LED corrente */
  {
    int mask = leds_get();
    LOG_INFO("DBG final current_mode=%d  leds_mask=0x%02x\n",
             (int)current_mode, mask);
  }

  coap_notify_observers(&res_fertilizer);
  res_get_handler(request, response, buffer, preferred_size, offset);
}

/*---------------------------------------------------------------------------*/
/* Triggered by button press: manual refill confirmation */
static void
res_trigger_handler(void)
{
  LOG_INFO("Fertilizer refill confirmed (trigger)\n");

  fertilizer_needs_refill = false;
  leds_off(LEDS_RED);  // red off after refill

  /* Notify observers */
  coap_notify_observers(&res_fertilizer);
}

