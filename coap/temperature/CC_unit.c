#include "contiki.h"
#include "net/netstack.h"
#include "os/dev/leds.h"
#include "sys/etimer.h"
#include "dev/button-hal.h"
#include "routing/routing.h"
#include "coap-engine.h"
#include "coap-blocking-api.h"

#include "sys/log.h"
#define LOG_MODULE "CC unit"
#define LOG_LEVEL LOG_LEVEL_INFO

#define START_INTERVAL 5
#define SERVER_EP "coap://[fd00::1]:5683"
#define REGISTRATION_RESOURCE "/registration"
#define REGISTRATION_RETRY_INTERVAL 2

extern coap_resource_t res_cc_fan;
extern coap_resource_t res_cc_heater;

static bool connected = false;
static bool registered = false;
static bool alert_active = false;

static struct etimer wait_connection;
static struct etimer registration_timer;
static struct etimer button_timer;

PROCESS(cc_unit_process, "Climate Control Unit");
AUTOSTART_PROCESSES(&cc_unit_process);

// --- Registration handler ---
static coap_endpoint_t server_ep;
static coap_message_t request[1];

void client_chunk_handler(coap_message_t *response) {
  const uint8_t *chunk;
  if(response == NULL) {
    LOG_INFO("Registration request timed out\n");
    etimer_set(&registration_timer, CLOCK_SECOND * REGISTRATION_RETRY_INTERVAL);
    return;
  }

  int len = coap_get_payload(response, &chunk);
  if(len > 0 && strncmp((char *)chunk, "Success", len) == 0) {
    registered = true;
    LOG_INFO("Registration successful\n");
  } else {
    LOG_INFO("Registration failed, retrying...\n");
    etimer_set(&registration_timer, CLOCK_SECOND * REGISTRATION_RETRY_INTERVAL);
  }
}

// --- Alert logic ---
static void trigger_alert(void) {
  alert_active = true;
  leds_single_on(LEDS_RED);
  LOG_INFO("ALERT: Human intervention required! (Red LED ON)\n");
}

static void resolve_alert(void) {
  LOG_INFO("Alert acknowledged by user. Blinking red LED for 5 seconds...\n");
  for(int i = 0; i < 10; i++) {
    leds_toggle(LEDS_RED);
    clock_wait(CLOCK_SECOND / 2);
  }
  leds_single_off(LEDS_RED);
  alert_active = false;
  LOG_INFO("Alert cleared (Red LED OFF)\n");

  coap_notify_observers(&res_cc_fan);
  coap_notify_observers(&res_cc_heater);
}

PROCESS_THREAD(cc_unit_process, ev, data)
{
  static struct button_hal_button *btn;

  PROCESS_BEGIN();

  coap_activate_resource(&res_cc_fan, "cc/fan");
  coap_activate_resource(&res_cc_heater, "cc/heater");

  LOG_INFO("Connecting to the Border Router...\n");
  etimer_set(&wait_connection, CLOCK_SECOND * START_INTERVAL);

  while(!connected) {
    PROCESS_WAIT_UNTIL(etimer_expired(&wait_connection));
    if(NETSTACK_ROUTING.node_is_reachable()) {
      LOG_INFO("Connected to the Border Router!\n");
      connected = true;
    } else {
      etimer_reset(&wait_connection);
    }
  }

  // Registration phase
  coap_endpoint_parse(SERVER_EP, strlen(SERVER_EP), &server_ep);
  etimer_set(&registration_timer, CLOCK_SECOND * REGISTRATION_RETRY_INTERVAL);

  while(!registered) {
    PROCESS_WAIT_UNTIL(etimer_expired(&registration_timer));
    LOG_INFO("Sending registration message\n");

    coap_init_message(request, COAP_TYPE_CON, COAP_POST, 0);
    coap_set_header_uri_path(request, REGISTRATION_RESOURCE);

    const char msg[] = "{\"device\":\"cc_unit\"}";
    coap_set_payload(request, (uint8_t *)msg, sizeof(msg) - 1);

    COAP_BLOCKING_REQUEST(&server_ep, request, client_chunk_handler);
  }

  LOG_INFO("CC Unit registered and ready\n");

  btn = button_hal_get_by_index(0);
  while(1) {
    PROCESS_YIELD();

    if(ev == button_hal_press_event && alert_active) {
      LOG_INFO("Button pressed, waiting for 5 seconds hold...\n");
      etimer_set(&button_timer, CLOCK_SECOND * 5);
      PROCESS_WAIT_EVENT_UNTIL(ev == button_hal_release_event || etimer_expired(&button_timer));
      if(etimer_expired(&button_timer)) {
        LOG_INFO("Button held for 5 seconds, resolving alert.\n");
        resolve_alert();
      } else {
        LOG_INFO("Button released before 5 seconds, alert not cleared.\n");
      }
    }
  }

  PROCESS_END();
}
