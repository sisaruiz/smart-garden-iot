#include <stdio.h>
#include <string.h>
#include "contiki.h"
#include "net/netstack.h"
#include "routing/routing.h"
#include "sys/etimer.h"
#include "coap-engine.h"
#include "coap-blocking-api.h"
#include "sys/log.h"

#define LOG_MODULE "grow_light"
#define LOG_LEVEL LOG_LEVEL_INFO

#define SERVER_EP "coap://[fd00::1]:5683"
#define REGISTRATION_RESOURCE "/registration"

// CoAP resource to activate
extern coap_resource_t res_grow_light;

// State variables
static bool connected = false;
static bool registered = false;

// Timers
static struct etimer connection_timer;
static struct etimer registration_timer;

// Retry intervals
#define CONNECTION_RETRY_INTERVAL 2
#define REGISTRATION_RETRY_INTERVAL 2

PROCESS(grow_light_process, "Grow Light Controller");
AUTOSTART_PROCESSES(&grow_light_process);

//-----------------------------------------------------------------------------//
// Border Router connectivity check
static bool is_connected() {
  if(NETSTACK_ROUTING.node_is_reachable()) {
    LOG_INFO("The Border Router is reachable\n");
    return true;
  } else {
    LOG_INFO("Waiting for connection with the Border Router\n");
    return false;
  }
}

//-----------------------------------------------------------------------------//
// Registration response handler
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

//-----------------------------------------------------------------------------//
PROCESS_THREAD(grow_light_process, ev, data)
{
  static coap_endpoint_t server_ep;
  static coap_message_t request[1];

  PROCESS_BEGIN();

  // Activate the CoAP resource
  coap_activate_resource(&res_grow_light, "actuators/grow_light");

  // Connection attempts
  etimer_set(&connection_timer, CLOCK_SECOND * CONNECTION_RETRY_INTERVAL);
  PROCESS_WAIT_UNTIL(etimer_expired(&connection_timer));
  while(!is_connected()) {
    etimer_reset(&connection_timer);
    PROCESS_WAIT_UNTIL(etimer_expired(&connection_timer));
  }
  connected = true;

  // Registration attempts
  etimer_set(&registration_timer, CLOCK_SECOND * REGISTRATION_RETRY_INTERVAL);
  while(!registered) {
    PROCESS_WAIT_UNTIL(etimer_expired(&registration_timer));

    LOG_INFO("Sending registration message\n");

    coap_endpoint_parse(SERVER_EP, strlen(SERVER_EP), &server_ep);

    coap_init_message(request, COAP_TYPE_CON, COAP_POST, 0);
    coap_set_header_uri_path(request, REGISTRATION_RESOURCE);

    const char msg[] = "{\"device\":\"grow_light\"}";
    coap_set_payload(request, (uint8_t *)msg, sizeof(msg) - 1);

    COAP_BLOCKING_REQUEST(&server_ep, request, client_chunk_handler);
  }

  LOG_INFO("Grow light actuator registered and ready\n");

  while(1) {
    PROCESS_YIELD();
  }

  PROCESS_END();
}
