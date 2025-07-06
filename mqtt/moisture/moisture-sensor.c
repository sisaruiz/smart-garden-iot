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
#include <stdlib.h> // for rand()

/*---------------------------------------------------------------------------*/
#define LOG_MODULE "moisture sensor"
#define LOG_LEVEL LOG_LEVEL_INFO

/*---------------------------------------------------------------------------*/
/* mqtt broker address. */
#define MQTT_CLIENT_BROKER_IP_ADDR "fd00::1"

static const char *broker_ip = MQTT_CLIENT_BROKER_IP_ADDR;

// default config values
#define DEFAULT_BROKER_PORT         1883
#define DEFAULT_PUBLISH_INTERVAL    (30 * CLOCK_SECOND)
#define SHORT_PUBLISH_INTERVAL      (8 * CLOCK_SECOND)

// we assume that the broker does not require authentication

/*---------------------------------------------------------------------------*/
/* various states */
static uint8_t state;

#define STATE_INIT        0
#define STATE_NET_OK      1
#define STATE_CONNECTING  2
#define STATE_CONNECTED   3
#define STATE_SUBSCRIBED  4
#define STATE_DISCONNECTED 5

/*---------------------------------------------------------------------------*/
PROCESS_NAME(mqtt_moisture_process);
AUTOSTART_PROCESSES(&mqtt_moisture_process);

/*---------------------------------------------------------------------------*/
/* maximum tcp segment size for outgoing segments of our socket */
#define MAX_TCP_SEGMENT_SIZE    32
#define CONFIG_IP_ADDR_STR_LEN   64
/*---------------------------------------------------------------------------*/
/*
 * buffers for client id and topics.
 * make sure they are large enough to hold the entire respective string
 */
#define BUFFER_SIZE 64

static char client_id[BUFFER_SIZE];
static char pub_topic[BUFFER_SIZE];
static char sub_topic[BUFFER_SIZE];

// periodic timer to check the state of the mqtt client
#define STATE_MACHINE_PERIODIC     (CLOCK_SECOND >> 1)
static struct etimer periodic_timer;

/*---------------------------------------------------------------------------*/
/*
 * the main mqtt buffers.
 * we will need to increase if we start publishing more data.
 */
#define APP_BUFFER_SIZE 512
static char app_buffer[APP_BUFFER_SIZE];
/*---------------------------------------------------------------------------*/
static struct mqtt_message *msg_ptr = 0;

static struct mqtt_connection conn;

/*---------------------------------------------------------------------------*/
PROCESS(mqtt_moisture_process, "mqtt moisture client");

/*---------------------------------------------------------------------------*/

// variable to simulate actuator influence (irrigation state ON/OFF)
static bool irrigation_on = false;

static void
pub_handler(const char *topic, uint16_t topic_len, const uint8_t *chunk,
            uint16_t chunk_len)
{
  if(strcmp(topic, "irrigation") == 0) {
    if(strncmp((const char*)chunk, "ON", chunk_len) == 0) {
      irrigation_on = true;
    } else if(strncmp((const char*)chunk, "OFF", chunk_len) == 0) {
      irrigation_on = false;
    }
    return;
  }
}

/*---------------------------------------------------------------------------*/
static void
mqtt_event(struct mqtt_connection *m, mqtt_event_t event, void *data)
{
  switch(event) {
  case MQTT_EVENT_CONNECTED: {
    LOG_INFO("application has a mqtt connection\n");
    state = STATE_CONNECTED;
    break;
  }
  case MQTT_EVENT_DISCONNECTED: {
    LOG_INFO("mqtt disconnect. reason %u\n", *((mqtt_event_t *)data));
    state = STATE_DISCONNECTED;
    process_poll(&mqtt_moisture_process);
    break;
  }
  case MQTT_EVENT_PUBLISH: {
    msg_ptr = data;
    pub_handler(msg_ptr->topic, strlen(msg_ptr->topic),
                msg_ptr->payload_chunk, msg_ptr->payload_length);
    break;
  }
  case MQTT_EVENT_SUBACK: {
#if MQTT_311
    mqtt_suback_event_t *suback_event = (mqtt_suback_event_t *)data;
    if(suback_event->success) {
      LOG_INFO("subscribed to topic successfully\n");
    } else {
      LOG_INFO("failed to subscribe to topic (ret code %x)\n", suback_event->return_code);
    }
#else
    LOG_INFO("subscribed to topic successfully\n");
#endif
    break;
  }
  case MQTT_EVENT_UNSUBACK: {
    LOG_INFO("unsubscribed from topic successfully\n");
    break;
  }
  case MQTT_EVENT_PUBACK: {
    LOG_INFO("publishing complete.\n");
    break;
  }
  default:
    LOG_INFO("unhandled mqtt event: %i\n", event);
    break;
  }
}

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
/*----------------------------------simulation-------------------------------*/
/*---------------------------------------------------------------------------*/

/* initialized soil moisture percentage */
static float moisture_value = 40.0;

/* values to simulate variation */
static float max_moisture_variation = 1.5;
static float irrigation_effect = 2.5; // irrigation increases moisture faster

static void change_moisture_simulation(){
  if(irrigation_on){
    moisture_value += irrigation_effect;
    if(moisture_value > 100.0) {
      moisture_value = 100.0; // max 100%
    }
  } else {
    // natural decrease or slight variation
    int decision = rand() % 3;
    float variation = (float)rand()/(float)(RAND_MAX/max_moisture_variation);
    switch(decision){
      case 0:
        break; // no change
      case 1:
        moisture_value += variation; // slight increase (e.g., dew)
        if(moisture_value > 100.0) moisture_value = 100.0;
        break;
      case 2:
        moisture_value -= variation; // slight decrease (drying)
        if(moisture_value < 0.0) moisture_value = 0.0;
        break;
    }
  }
}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/

PROCESS_THREAD(mqtt_moisture_process, ev, data)
{
  PROCESS_BEGIN();
  
  mqtt_status_t status;
  char broker_address[CONFIG_IP_ADDR_STR_LEN];

  LOG_INFO("mqtt moisture process\n");

  // initialize clientID as MAC address
  snprintf(client_id, BUFFER_SIZE, "%02x%02x%02x%02x%02x%02x",
           linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1],
           linkaddr_node_addr.u8[2], linkaddr_node_addr.u8[5],
           linkaddr_node_addr.u8[6], linkaddr_node_addr.u8[7]);

  // register mqtt client
  mqtt_register(&conn, &mqtt_moisture_process, client_id, mqtt_event,
                MAX_TCP_SEGMENT_SIZE);
          
  state = STATE_INIT;
            
  // set periodic timer
  etimer_set(&periodic_timer, STATE_MACHINE_PERIODIC);

  while(1) {
    PROCESS_YIELD();

    if((ev == PROCESS_EVENT_TIMER && data == &periodic_timer) ||
       ev == PROCESS_EVENT_POLL){

      if(state == STATE_INIT){
        if(have_connectivity() == true)
          state = STATE_NET_OK;
      }

      if(state == STATE_NET_OK){
        LOG_INFO("connecting to mqtt server\n");
        memcpy(broker_address, broker_ip, strlen(broker_ip));
        mqtt_connect(&conn, broker_address, DEFAULT_BROKER_PORT,
                     (DEFAULT_PUBLISH_INTERVAL * 3) / CLOCK_SECOND,
                     MQTT_CLEAN_SESSION_ON);
        state = STATE_CONNECTING;
      }

      if(state == STATE_CONNECTED){
        // subscribe to irrigation actuator topic to simulate influence
        strcpy(sub_topic, "irrigation");
        status = mqtt_subscribe(&conn, NULL, sub_topic, MQTT_QOS_LEVEL_0);
        LOG_INFO("subscribing to topic irrigation for simulation\n");
        if(status == MQTT_STATUS_OUT_QUEUE_FULL) {
          LOG_ERR("command queue full, cannot subscribe to irrigation\n");
        } else {
          state = STATE_SUBSCRIBED;
        }
      }

      if(state == STATE_SUBSCRIBED){
        // simulate moisture changes
        change_moisture_simulation();

        // publish moisture sensor value with one decimal
        sprintf(pub_topic, "%s", "soilMoisture");
        sprintf(app_buffer, "{\"soilMoisture\":%.1f}", moisture_value);

        mqtt_publish(&conn, NULL, pub_topic, (uint8_t *)app_buffer,
                     strlen(app_buffer), MQTT_QOS_LEVEL_0, MQTT_RETAIN_OFF);
      }
      else if(state == STATE_DISCONNECTED){
        LOG_ERR("disconnected from mqtt broker\n");
        state = STATE_INIT;
      }

      etimer_set(&periodic_timer, SHORT_PUBLISH_INTERVAL);
    }
  }

  PROCESS_END();
}
