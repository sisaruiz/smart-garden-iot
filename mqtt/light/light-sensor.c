#include "contiki.h"
#include "net/routing/routing.h"
#include "mqtt.h"
#include "net/ipv6/uip.h"
#include "net/ipv6/uip-icmp6.h"
#include "net/ipv6/sicslowpan.h"
#include "sys/etimer.h"
#include "sys/ctimer.h"
#include "lib/sensors.h"
#include "dev/button-hal.h"
#include "dev/leds.h"
#include "os/sys/log.h"
#include "mqtt-client.h"

#include <string.h>
#include <strings.h>
#include <stdlib.h>

/*---------------------------------------------------------------------------*/
#define LOG_MODULE "Light Sensor"
#define LOG_LEVEL LOG_LEVEL_INFO
/*---------------------------------------------------------------------------*/
/* MQTT broker address */
#define MQTT_CLIENT_BROKER_IP_ADDR "fd00::1"
static const char *broker_ip = MQTT_CLIENT_BROKER_IP_ADDR;

/* Default config */
#define DEFAULT_BROKER_PORT         1883
#define DEFAULT_PUBLISH_INTERVAL    (30 * CLOCK_SECOND)
#define SHORT_PUBLISH_INTERVAL      (8 * CLOCK_SECOND)

/*---------------------------------------------------------------------------*/
/* MQTT client states */
static uint8_t state;

#define STATE_INIT         0
#define STATE_NET_OK       1
#define STATE_CONNECTING   2
#define STATE_CONNECTED    3
#define STATE_SUBSCRIBED   4
#define STATE_DISCONNECTED 5

/*---------------------------------------------------------------------------*/
PROCESS_NAME(mqtt_light_process);
AUTOSTART_PROCESSES(&mqtt_light_process);

/*---------------------------------------------------------------------------*/
/* Buffers */
#define MAX_TCP_SEGMENT_SIZE    32
#define CONFIG_IP_ADDR_STR_LEN   64
#define BUFFER_SIZE 64

static char client_id[BUFFER_SIZE];
static char pub_topic[BUFFER_SIZE];
static char sub_topic[BUFFER_SIZE];

static struct etimer periodic_timer;

/* MQTT buffers */
#define APP_BUFFER_SIZE 512
static char app_buffer[APP_BUFFER_SIZE];

static struct mqtt_message *msg_ptr = 0;
static struct mqtt_connection conn;

/*---------------------------------------------------------------------------*/
/* Simulation variables */

// grow_light actuator state: 0 = OFF, 1 = ON, 2 = DIM
static int grow_light_state = 0;

// Simulated light value (lux)
static float light_value = 300.0f; // initial normal daylight lux

// Constants for simulation
static float max_light_variation = 20.0f;
static float grow_light_influence = 100.0f;

/*---------------------------------------------------------------------------*/
/* Handler for actuator commands received on MQTT */
static void
pub_handler(const char *topic, uint16_t topic_len, const uint8_t *chunk,
            uint16_t chunk_len)
{
  if(strcmp(topic, "growLight") == 0) {
    if(strncmp((const char *)chunk, "OFF", chunk_len) == 0) {
      grow_light_state = 0;
      LOG_INFO("Received growLight OFF command\n");
    } else if(strncmp((const char *)chunk, "ON", chunk_len) == 0) {
      grow_light_state = 1;
      LOG_INFO("Received growLight ON command\n");
    } else if(strncmp((const char *)chunk, "DIM", chunk_len) == 0) {
      grow_light_state = 2;
      LOG_INFO("Received growLight DIM command\n");
    }
  }
}

/*---------------------------------------------------------------------------*/
static void
mqtt_event(struct mqtt_connection *m, mqtt_event_t event, void *data)
{
  switch(event) {
  case MQTT_EVENT_CONNECTED:
    LOG_INFO("MQTT connected\n");
    state = STATE_CONNECTED;
    break;

  case MQTT_EVENT_DISCONNECTED:
    LOG_INFO("MQTT disconnected, reason %u\n", *((mqtt_event_t *)data));
    state = STATE_DISCONNECTED;
    process_poll(&mqtt_light_process);
    break;

  case MQTT_EVENT_PUBLISH:
    msg_ptr = data;
    pub_handler(msg_ptr->topic, strlen(msg_ptr->topic),
                msg_ptr->payload_chunk, msg_ptr->payload_length);
    break;

  case MQTT_EVENT_SUBACK:
#if MQTT_311
    {
      mqtt_suback_event_t *suback_event = (mqtt_suback_event_t *)data;
      if(suback_event->success) {
        LOG_INFO("Subscribed successfully\n");
      } else {
        LOG_INFO("Failed to subscribe, code %x\n", suback_event->return_code);
      }
    }
#else
    LOG_INFO("Subscribed successfully\n");
#endif
    break;

  case MQTT_EVENT_UNSUBACK:
    LOG_INFO("Unsubscribed successfully\n");
    break;

  case MQTT_EVENT_PUBACK:
    LOG_INFO("Publish complete\n");
    break;

  default:
    LOG_INFO("Unhandled MQTT event: %i\n", event);
    break;
  }
}

/*---------------------------------------------------------------------------*/
static bool
have_connectivity(void)
{
  if(uip_ds6_get_global(ADDR_PREFERRED) == NULL ||
     uip_ds6_defrt_choose() == NULL) {
    return false;
  }
  return true;
}

/*---------------------------------------------------------------------------*/
/* Simulate light value affected by grow_light actuator */
static void
simulate_light_change(void)
{
  // Random small fluctuation
  int decision = rand() % 3;
  float variation = (float)rand()/(float)(RAND_MAX / max_light_variation);

  switch(decision) {
    case 0:
      // no change
      break;
    case 1:
      light_value += variation;
      break;
    case 2:
      light_value -= variation;
      break;
  }

  // Clamp light_value to realistic bounds
  if(light_value < 0) light_value = 0;
  if(light_value > 10000) light_value = 10000;

  // Adjust light_value according to grow_light actuator state
  switch(grow_light_state) {
    case 0: // OFF
      // no additional light added
      break;
    case 1: // ON
      light_value += grow_light_influence;
      break;
    case 2: // DIM
      light_value += grow_light_influence / 2;
      break;
  }

  // Clamp again after adjustment
  if(light_value < 0) light_value = 0;
  if(light_value > 10000) light_value = 10000;
}

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(mqtt_light_process, ev, data)
{
  PROCESS_BEGIN();

  mqtt_status_t status;
  char broker_address[CONFIG_IP_ADDR_STR_LEN];

  LOG_INFO("Starting MQTT Light Sensor process\n");

  // Initialize client ID using MAC address
  snprintf(client_id, BUFFER_SIZE, "%02x%02x%02x%02x%02x%02x",
           linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1],
           linkaddr_node_addr.u8[2], linkaddr_node_addr.u8[5],
           linkaddr_node_addr.u8[6], linkaddr_node_addr.u8[7]);

  // Register MQTT client
  mqtt_register(&conn, &mqtt_light_process, client_id, mqtt_event, MAX_TCP_SEGMENT_SIZE);

  state = STATE_INIT;

  etimer_set(&periodic_timer, STATE_MACHINE_PERIODIC);

  while(1) {
    PROCESS_YIELD();

    if((ev == PROCESS_EVENT_TIMER && data == &periodic_timer) ||
       ev == PROCESS_EVENT_POLL) {

      if(state == STATE_INIT) {
        if(have_connectivity()) {
          state = STATE_NET_OK;
        }
      }

      if(state == STATE_NET_OK) {
        LOG_INFO("Connecting to MQTT broker\n");
        memcpy(broker_address, broker_ip, strlen(broker_ip));
        mqtt_connect(&conn, broker_address, DEFAULT_BROKER_PORT,
                     (DEFAULT_PUBLISH_INTERVAL * 3) / CLOCK_SECOND,
                     MQTT_CLEAN_SESSION_ON);
        state = STATE_CONNECTING;
      }

      if(state == STATE_CONNECTED) {
        // Subscribe to actuator topic "growLight"
        strcpy(sub_topic, "growLight");
        status = mqtt_subscribe(&conn, NULL, sub_topic, MQTT_QOS_LEVEL_0);
        LOG_INFO("Subscribing to growLight actuator topic\n");
        if(status == MQTT_STATUS_OUT_QUEUE_FULL) {
          LOG_ERR("Command queue full, cannot subscribe\n");
          PROCESS_EXIT();
        }
        state = STATE_SUBSCRIBED;
      }

      if(state == STATE_SUBSCRIBED) {
        // Simulate sensor reading influenced by grow_light actuator
        simulate_light_change();

        // Publish light sensor value in JSON format
        sprintf(pub_topic, "light");
        sprintf(app_buffer, "{\"light\":%.2f}", light_value);

        mqtt_publish(&conn, NULL, pub_topic, (uint8_t *)app_buffer,
                     strlen(app_buffer), MQTT_QOS_LEVEL_0, MQTT_RETAIN_OFF);
      } else if(state == STATE_DISCONNECTED) {
        LOG_ERR("Disconnected from MQTT broker\n");
        state = STATE_INIT;
      }

      etimer_set(&periodic_timer, SHORT_PUBLISH_INTERVAL);
    }
  }

  PROCESS_END();
}
