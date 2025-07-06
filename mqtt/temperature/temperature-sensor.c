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
#include <stdlib.h>  // for rand()

/*---------------------------------------------------------------------------*/
#define LOG_MODULE "mqtt-client"
#ifdef MQTT_CLIENT_CONF_LOG_LEVEL
#define LOG_LEVEL MQTT_CLIENT_CONF_LOG_LEVEL
#else
#define LOG_LEVEL LOG_LEVEL_DBG
#endif

/*---------------------------------------------------------------------------*/
/* mqtt broker address */
#define MQTT_CLIENT_BROKER_IP_ADDR "fd00::1"

static const char *broker_ip = MQTT_CLIENT_BROKER_IP_ADDR;

// default config values
#define DEFAULT_BROKER_PORT         1883
#define DEFAULT_PUBLISH_INTERVAL    (30 * CLOCK_SECOND)
#define SHORT_PUBLISH_INTERVAL      (8 * CLOCK_SECOND)

/*---------------------------------------------------------------------------*/
/* various states */
static uint8_t state;

#define STATE_INIT         0
#define STATE_NET_OK       1
#define STATE_CONNECTING   2
#define STATE_CONNECTED    3
#define STATE_SUBSCRIBED   4
#define STATE_DISCONNECTED 5

/*---------------------------------------------------------------------------*/
PROCESS_NAME(mqtt_temperature_process);
AUTOSTART_PROCESSES(&mqtt_temperature_process);

/*---------------------------------------------------------------------------*/
/* maximum tcp segment size */
#define MAX_TCP_SEGMENT_SIZE    32
#define CONFIG_IP_ADDR_STR_LEN   64
/*---------------------------------------------------------------------------*/
/* buffers for client id and topics */
#define BUFFER_SIZE 64

static char client_id[BUFFER_SIZE];
static char pub_topic[BUFFER_SIZE];
static char sub_topic1[BUFFER_SIZE];
static char sub_topic2[BUFFER_SIZE];

// periodic timer to check mqtt client state
#define STATE_MACHINE_PERIODIC     (CLOCK_SECOND >> 1)
static struct etimer periodic_timer;

/*---------------------------------------------------------------------------*/
/* main mqtt buffers, increase if more data is published */
#define APP_BUFFER_SIZE 512
static char app_buffer[APP_BUFFER_SIZE];
/*---------------------------------------------------------------------------*/
static struct mqtt_message *msg_ptr = 0;

static struct mqtt_connection conn;

/*---------------------------------------------------------------------------*/
PROCESS(mqtt_temperature_process, "mqtt temperature client");

/*---------------------------------------------------------------------------*/

// variables to simulate the effect of coap actuators on temperature sensor
static bool heater_on = false;
static bool fan_on = false;
static bool heater_subscribed = false;
static bool fan_subscribed = false;

/* when heater or fan messages are received, update simulation variables */
static void
pub_handler(const char *topic, uint16_t topic_len, const uint8_t *chunk,
            uint16_t chunk_len)
{
  // topic related to the fan status (simulation)
  if(strcmp(topic, "fan") == 0) {
    if(strcmp((const char*) chunk, "on") == 0) { // fan activated
      // fan and heater cannot be on simultaneously
      heater_on = false;
      fan_on = true;
    } else if(strcmp((const char*) chunk, "off") == 0) { // fan stopped
      fan_on = false;
    }
    return;
  }
  // topic related to the heater status (simulation)
  else if(strcmp(topic, "heater") == 0){
    if(strcmp((const char*) chunk, "on") == 0) { // heater activated
      // fan and heater cannot be on simultaneously
      fan_on = false;
      heater_on = true;
    } else if(strcmp((const char*) chunk, "off") == 0) { // heater stopped
      heater_on = false;
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
    process_poll(&mqtt_temperature_process);
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
      LOG_INFO("application subscribed to topic successfully\n");
    } else {
      LOG_INFO("application failed to subscribe to topic (ret code %x)\n", suback_event->return_code);
    }
#else
    LOG_INFO("application subscribed to topic successfully\n");
#endif
    break;
  }
  case MQTT_EVENT_UNSUBACK: {
    LOG_INFO("unsubscribed from topic successfully\n");
    break;
  }
  case MQTT_EVENT_PUBACK: {
    LOG_INFO("publish acknowledged\n");
    break;
  }
  default:
    LOG_INFO("application got an unhandled mqtt event: %i\n", event);
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
/*-----------------------------simulation------------------------------------*/
/*---------------------------------------------------------------------------*/

/* initialized temperature value */
static float temperature_value = 25.0;

/* maximum temperature variation for simulation */
static float max_temperature_variation = 0.2;
static float temperature_variation_controller = 0.4;

/* simulate temperature changes affected by fan and heater */
static void change_temperature_simulation(){
  char fractional_part[3];
  sprintf(fractional_part, "%d", (int)((temperature_value - (int)temperature_value) * 10));
  LOG_INFO("current temperature: %d.%s\n", (int)temperature_value, fractional_part);

  if((fan_on == false) && (heater_on == false)){
    int decision = rand() % 3;
    switch(decision){
      case 0: break; // no variation
      case 1: temperature_value += max_temperature_variation; break; // increase
      case 2: temperature_value -= max_temperature_variation; break; // decrease
    }
  }
  else if(fan_on == true){
    temperature_value -= temperature_variation_controller;
  }
  else if(heater_on == true){
    temperature_value += temperature_variation_controller;
  }

  sprintf(fractional_part, "%d", (int)((temperature_value - (int)temperature_value) * 100));
  LOG_INFO("new temperature: %d.%s\n", (int)temperature_value, fractional_part);
}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/

PROCESS_THREAD(mqtt_temperature_process, ev, data)
{
  PROCESS_BEGIN();
  
  mqtt_status_t status;
  char broker_address[CONFIG_IP_ADDR_STR_LEN];

  LOG_INFO("mqtt temperature process\n");

  // initialize client id as mac address
  snprintf(client_id, BUFFER_SIZE, "%02x%02x%02x%02x%02x%02x",
           linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1],
           linkaddr_node_addr.u8[2], linkaddr_node_addr.u8[5],
           linkaddr_node_addr.u8[6], linkaddr_node_addr.u8[7]);

  // register mqtt client
  mqtt_register(&conn, &mqtt_temperature_process, client_id, mqtt_event,
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
        LOG_INFO("[temperature device] connecting to mqtt server\n");
        snprintf(broker_address, CONFIG_IP_ADDR_STR_LEN, "%s", broker_ip);
        mqtt_connect(&conn, broker_address, DEFAULT_BROKER_PORT,
                     (DEFAULT_PUBLISH_INTERVAL * 3) / CLOCK_SECOND,
                     MQTT_CLEAN_SESSION_ON);
        state = STATE_CONNECTING;
      }
      
      if(state == STATE_CONNECTED){
        if(fan_subscribed == false){
          strcpy(sub_topic1,"fan");
          status = mqtt_subscribe(&conn, NULL, sub_topic1, MQTT_QOS_LEVEL_0);
          LOG_INFO("[temperature device] subscribing to topic fan\n");
          if(status == MQTT_STATUS_OUT_QUEUE_FULL) {
            LOG_ERR("[temperature device] command queue full, cannot subscribe to fan\n");
          } else {
            fan_subscribed = true;
          }
        } else if(heater_subscribed == false){
          strcpy(sub_topic2,"heater");
          status = mqtt_subscribe(&conn, NULL, sub_topic2, MQTT_QOS_LEVEL_0);
          LOG_INFO("[temperature device] subscribing to topic heater\n");
          if(status == MQTT_STATUS_OUT_QUEUE_FULL) {
            LOG_ERR("[temperature device] command queue full, cannot subscribe to heater\n");
          } else {
            heater_subscribed = true;
          }
        } else if(fan_subscribed == true && heater_subscribed == true){
          state = STATE_SUBSCRIBED;
        }
      }
      
      if(state == STATE_SUBSCRIBED){
        sprintf(pub_topic, "temperature");
        change_temperature_simulation();

        char fractional_part[3];
        sprintf(fractional_part, "%d", (int)((temperature_value - (int)temperature_value) * 10));
        sprintf(app_buffer, "{\"temperature\":%d.%s}", (int)temperature_value, fractional_part);

        mqtt_publish(&conn, NULL, pub_topic, (uint8_t *)app_buffer,
                     strlen(app_buffer), MQTT_QOS_LEVEL_0, MQTT_RETAIN_OFF);
      }
      else if(state == STATE_DISCONNECTED){
        LOG_ERR("[temperature device] disconnected from mqtt broker\n");
        state = STATE_INIT;
        process_poll(&mqtt_temperature_process);
      }
      
      etimer_set(&periodic_timer, SHORT_PUBLISH_INTERVAL);
    }
  }

  PROCESS_END();
}
