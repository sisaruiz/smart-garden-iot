#include "contiki.h"
#include "net/netstack.h"
#include "os/dev/leds.h"
#include "sys/etimer.h"
#include "os/dev/button-hal.h"
#include "routing/routing.h"
#include "coap-engine.h"
#include "coap-blocking-api.h"
#include "random.h"

#include "sys/log.h"
#define LOG_MODULE "coap device"
#define LOG_LEVEL LOG_LEVEL_INFO

#define SERVER_EP "coap://[fd00::1]:5683"
#define START_INTERVAL 1
#define REGISTRATION_INTERVAL 1
#define FLOW_BASE_INTERVAL 15     // Base trigger interval in seconds
#define FLOW_JITTER 5             // Jitter in seconds (+/- up to 5s)

// Declare actuator resources (must be implemented in their respective folders)
extern coap_resource_t res_fertilizer;
extern coap_resource_t res_irrigation;
extern coap_resource_t res_grow_light;
extern coap_resource_t res_cc_fan;
extern coap_resource_t res_cc_heater;

// Service URL
static char *service_url = "/registration";

// State flags
static bool connected = false;
static bool registered = false;

// Timers
static struct etimer wait_connection;
static struct etimer wait_registration;
static struct etimer flow_timer;

// Button
static struct button_hal_button *btn;

// Registration response handler
void client_chunk_handler(coap_message_t *response)
{
  const uint8_t *chunk;
  if(response == NULL) {
    LOG_INFO("Request timed out\n");
    etimer_reset(&wait_registration);
    return;
  }
  coap_get_payload(response, &chunk);
  if(chunk && strncmp((char*)chunk, "Success", 7) == 0) {
    LOG_INFO("Registration completed!\n");
    leds_single_off(LEDS_BLUE);
    leds_set(LEDS_GREEN);
    registered = true;
  } else {
    LOG_INFO("Sending a new registration request...\n");
    etimer_reset(&wait_registration);
  }
}

/*---------------------------------------------------------------------------*/
PROCESS(coap_device, "CoAP device");
AUTOSTART_PROCESSES(&coap_device);

PROCESS_THREAD(coap_device, ev, data)
{
  static coap_endpoint_t server_ep;
  static coap_message_t request[1];

  PROCESS_BEGIN();

  PROCESS_PAUSE();

  coap_endpoint_parse(SERVER_EP, strlen(SERVER_EP), &server_ep);

  etimer_set(&wait_connection, CLOCK_SECOND * START_INTERVAL);

  // Blue LED: not connected
  leds_single_on(LEDS_BLUE);

  // Activate actuator resources
  coap_activate_resource(&res_fertilizer, "actuators/fertilizer");
  coap_activate_resource(&res_irrigation, "actuators/irrigation");
  coap_activate_resource(&res_grow_light, "actuators/grow_light");
  coap_activate_resource(&res_cc_fan, "cc/fan");
  coap_activate_resource(&res_cc_heater, "cc/heater");

  LOG_INFO("Connecting to the Border Router...\n");

  while(!connected){
    PROCESS_WAIT_UNTIL(etimer_expired(&wait_connection));
    if(NETSTACK_ROUTING.node_is_reachable()){
      LOG_INFO("Connected to the Border Router!\n");
      connected = true;
      leds_single_off(LEDS_BLUE);
    } else {
      etimer_reset(&wait_connection);
    }
  }

  LOG_INFO("Registering to the CoAP Network Controller...\n");
  etimer_set(&wait_registration, CLOCK_SECOND * REGISTRATION_INTERVAL);

  while(!registered){
    PROCESS_WAIT_UNTIL(etimer_expired(&wait_registration));
    leds_toggle(LEDS_BLUE);

    coap_init_message(request, COAP_TYPE_CON, COAP_POST, 0);
    coap_set_header_uri_path(request, service_url);

    // Static configuration: device and resource list
    const char msg[] = "{\"device\":\"coapDevice\",\"resources\":[\"actuators/fertilizer\",\"actuators/irrigation\",\"actuators/grow_light\",\"cc/fan\",\"cc/heater\"]}";
    coap_set_payload(request, (uint8_t *)msg, sizeof(msg) - 1);

    COAP_BLOCKING_REQUEST(&server_ep, request, client_chunk_handler);
  }

  LOG_INFO("Device started correctly!\n");

  // Start FLOW trigger timer with jitter
  int base = FLOW_BASE_INTERVAL * CLOCK_SECOND;
  int jitter = (random_rand() % (2 * FLOW_JITTER * CLOCK_SECOND)) - (FLOW_JITTER * CLOCK_SECOND);
  etimer_set(&flow_timer, base + jitter);

  btn = button_hal_get_by_index(0);
  while(1) {
    PROCESS_WAIT_EVENT();

    if(ev == PROCESS_EVENT_TIMER && etimer_expired(&flow_timer)) {
      res_fertilizer.trigger();
      res_irrigation.trigger();
      res_grow_light.trigger();
      res_cc_fan.trigger();
      res_cc_heater.trigger();

      int new_jitter = (random_rand() % (2 * FLOW_JITTER * CLOCK_SECOND)) - (FLOW_JITTER * CLOCK_SECOND);
      etimer_set(&flow_timer, FLOW_BASE_INTERVAL * CLOCK_SECOND + new_jitter);
    }

    // Button-based alert logic can go here if needed
  }

  PROCESS_END();
}
