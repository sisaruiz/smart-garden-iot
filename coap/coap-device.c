#include "contiki.h"
#include "net/netstack.h"
#include "os/dev/leds.h"
#include "sys/etimer.h"
#include "os/dev/button-hal.h"
#include "routing/routing.h"
#include "coap-engine.h"
#include "coap-blocking-api.h"

#include "sys/log.h"
#define LOG_MODULE "coap device"
#define LOG_LEVEL LOG_LEVEL_INFO

#define SERVER_EP "coap://[fd00::1]:5683"
#define START_INTERVAL 1
#define REGISTRATION_INTERVAL 1

// Declare actuator resources
extern coap_resource_t res_fertilizer;
extern coap_resource_t res_irrigation;
extern coap_resource_t res_grow_light;
extern coap_resource_t res_cc_fan;
extern coap_resource_t res_cc_heater;

// Init trigger functions
void fertilizer_resource_init(void);
void irrigation_resource_init(void);
void grow_light_resource_init(void);
void cc_fan_resource_init(void);
void cc_heater_resource_init(void);

// Service URL
static char *service_url = "/registration";

// State flags
static bool connected = false;
static bool registered = false;

// Timers
static struct etimer wait_connection;
static struct etimer wait_registration;
static struct etimer feedback_led_timer;
static bool feedback_led_on = false;

// Button
static button_hal_button_t *btn;

bool fertilizer_needs_refill = false;

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
 coap_activate_resource(&res_fertilizer, "fertilizer");
 coap_activate_resource(&res_irrigation, "irrigation");
 coap_activate_resource(&res_grow_light, "grow_light");
 coap_activate_resource(&res_cc_fan, "fan");
 coap_activate_resource(&res_cc_heater, "heater");


  // Assign trigger handlers for all actuators
  fertilizer_resource_init();
  irrigation_resource_init();
  grow_light_resource_init();
  cc_fan_resource_init();
  cc_heater_resource_init();

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

    static char msg[256];
    snprintf(msg, sizeof(msg),
     "{\"device\":\"coapDevice\",\"resources\":["
     "\"fertilizer\",\"irrigation\","
     "\"grow_light\",\"fan\",\"heater\"]}");

    LOG_INFO("Sending registration payload: %s\n", msg);

    coap_set_payload(request, (uint8_t *)msg, strlen(msg));
    COAP_BLOCKING_REQUEST(&server_ep, request, client_chunk_handler);
  }

  LOG_INFO("Device started correctly!\n");

  // Initialize button
  btn = button_hal_get_by_index(0);

  while(1) {
    PROCESS_WAIT_EVENT();
    
	  // 1) Auto-turn off feedback LEDs when the timer fires
	  if (ev == PROCESS_EVENT_TIMER && data == &feedback_led_timer && feedback_led_on) {
	    leds_off(LEDS_GREEN | LEDS_RED);
	    feedback_led_on = false;
	    continue; // optional: skip the rest of this loop iteration
	  }

    	// Check for fertilizer refill confirmation
        // Only react when the tank NEEDS a refill and the button is RELEASED
	if (ev == button_hal_release_event && fertilizer_needs_refill) {
	  btn = (button_hal_button_t *)data;
	  if (btn && btn->press_duration_seconds >= 3) {
	    LOG_INFO("Manual refill confirmed (>=3s hold)\n");
	    fertilizer_needs_refill = false;
	    leds_on(LEDS_GREEN);
	    res_fertilizer.trigger();   // notify/resource-side reset

	    etimer_set(&feedback_led_timer, CLOCK_SECOND / 2);
	    feedback_led_on = true;
	  } else {
	    LOG_INFO("Refill rejected (<3s hold)\n");
	    leds_on(LEDS_RED);

	    etimer_set(&feedback_led_timer, CLOCK_SECOND / 2);
	    feedback_led_on = true;
	  }
	}
  }

  PROCESS_END();
}

