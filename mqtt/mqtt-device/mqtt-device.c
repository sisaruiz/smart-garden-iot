#include "contiki.h"
#include "net/routing/routing.h"
#include "mqtt.h"
#include "net/ipv6/uip.h"
#include "net/ipv6/uip-icmp6.h"
#include "net/ipv6/sicslowpan.h"
#include "sys/etimer.h"
#include "lib/sensors.h"
#include "dev/leds.h"
#include "dev/etc/rgb-led/rgb-led.h"
#include "os/sys/log.h"
#include "mqtt-client.h"

#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
/*---------------------------------------------------------------------------*/
#define LOG_MODULE "mqtt-client"
#ifdef MQTT_CLIENT_CONF_LOG_LEVEL
#define LOG_LEVEL MQTT_CLIENT_CONF_LOG_LEVEL
#else
#define LOG_LEVEL LOG_LEVEL_DBG
#endif

/*---------------------------------------------------------------------------*/
/* MQTT broker address. */
#define MQTT_CLIENT_BROKER_IP_ADDR "fd00::1"

static const char *broker_ip = MQTT_CLIENT_BROKER_IP_ADDR;

// Default config values
#define DEFAULT_BROKER_PORT         1883
#define DEFAULT_PUBLISH_INTERVAL    (30 * CLOCK_SECOND)
#define SHORT_PUBLISH_INTERVAL (8*CLOCK_SECOND)

/*---------------------------------------------------------------------------*/
/* Various states */
static uint8_t state;

#define STATE_INIT    		  0
#define STATE_NET_OK    	  1
#define STATE_CONNECTING      2
#define STATE_CONNECTED       3
#define STATE_SUBSCRIBED      4
#define STATE_DISCONNECTED    5

/*---------------------------------------------------------------------------*/
PROCESS_NAME(mqtt_device_process);
AUTOSTART_PROCESSES(&mqtt_device_process);

/*---------------------------------------------------------------------------*/
/* Maximum TCP segment size for outgoing segments of our socket */
#define MAX_TCP_SEGMENT_SIZE    32
#define CONFIG_IP_ADDR_STR_LEN   64
/*---------------------------------------------------------------------------*/
/*
 * Buffers for Client ID and Topics.
 * Make sure they are large enough to hold the entire respective string
 */
#define BUFFER_SIZE 64

static char client_id[BUFFER_SIZE];
static char sub_topic[BUFFER_SIZE];
static char sub_topic_light[BUFFER_SIZE];
static char sub_topic_moisture[BUFFER_SIZE];
static char sub_topic_ph[BUFFER_SIZE];
static char sub_topic_temp[BUFFER_SIZE];

/* Periodic timer to check the state of the MQTT client */
#define STATE_MACHINE_PERIODIC     (CLOCK_SECOND >> 1)
static struct etimer periodic_timer;

/*---------------------------------------------------------------------------*/
/*
 * The main MQTT buffers.
 * We will need to increase if we start publishing more data.
 */
#define APP_BUFFER_SIZE 512

/*---------------------------------------------------------------------------*/
static struct mqtt_message *msg_ptr = 0;

static struct mqtt_connection conn;

/*---------------------------------------------------------------------------*/
// Actuator simulation variables
static int grow_light_state = 0; // 0=OFF, 1=ON, 2=DIM
static bool irrigation_on = false;
static int fertilizer_erogation_variation = 0;
static bool heater_on = false;
static bool fan_on = false;

// Sensor values received from sensor nodes
static float light_value = 0.0f;
static float moisture_value = 0.0f;
static float pH_value = 0.0f;
static float temperature_value = 0.0f;

// MQTT subscription state
static bool grow_light_subscribed = false;
static bool irrigation_subscribed = false;
static bool fertilizer_subscribed = false;
static bool fan_subscribed = false;
static bool heater_subscribed = false;
static bool light_subscribed = false;
static bool moisture_subscribed = false;
static bool ph_subscribed = false;
static bool temp_subscribed = false;

/*---------------------------------------------------------------------------*/
// Pub handler for actuators and sensor values
static void pub_handler(const char *topic, uint16_t topic_len, const uint8_t *chunk, uint16_t chunk_len) {
  if(strcmp(topic, "growLight") == 0) {
    if(strncmp((const char*)chunk, "OFF", chunk_len) == 0) grow_light_state = 0;
    else if(strncmp((const char*)chunk, "ON", chunk_len) == 0) grow_light_state = 1;
    else if(strncmp((const char*)chunk, "DIM", chunk_len) == 0) grow_light_state = 2;
  } else if(strcmp(topic, "irrigation") == 0) {
    if(strncmp((const char*)chunk, "ON", chunk_len) == 0) irrigation_on = true;
    else if(strncmp((const char*)chunk, "OFF", chunk_len) == 0) irrigation_on = false;
  } else if(strcmp(topic, "fertilizerDispenser") == 0) {
    if(strncmp((const char*)chunk, "OFF", chunk_len) == 0) fertilizer_erogation_variation = 0;
    else if(strncmp((const char*)chunk, "SDEC", chunk_len) == 0) fertilizer_erogation_variation = -1;
    else if(strncmp((const char*)chunk, "SINC", chunk_len) == 0) fertilizer_erogation_variation = 1;
    else if(strncmp((const char*)chunk, "DEC", chunk_len) == 0) fertilizer_erogation_variation = -2;
    else if(strncmp((const char*)chunk, "INC", chunk_len) == 0) fertilizer_erogation_variation = 2;
  } else if(strcmp(topic, "fan") == 0) {
    if(strncmp((const char*)chunk, "on", chunk_len) == 0) { heater_on = false; fan_on = true; }
    else if(strncmp((const char*)chunk, "off", chunk_len) == 0) fan_on = false;
  } else if(strcmp(topic, "heater") == 0) {
    if(strncmp((const char*)chunk, "on", chunk_len) == 0) { fan_on = false; heater_on = true; }
    else if(strncmp((const char*)chunk, "off", chunk_len) == 0) heater_on = false;
  } else if(strcmp(topic, "light") == 0) {
    // Parse JSON: {"light":<value>}
    char *colon = strchr((const char*)chunk, ':');
    if(colon) {
      light_value = atof(colon + 1);
    }
  } else if(strcmp(topic, "soilMoisture") == 0) {
    // Parse JSON: {"soilMoisture":<value>}
    char *colon = strchr((const char*)chunk, ':');
    if(colon) {
      moisture_value = atof(colon + 1);
    }
  } else if(strcmp(topic, "pH") == 0) {
    // Parse JSON: {"pH":<value>}
    char *colon = strchr((const char*)chunk, ':');
    if(colon) {
      pH_value = atof(colon + 1);
    }
  } else if(strcmp(topic, "temperature") == 0) {
    // Parse JSON: {"temperature":<value>}
    char *colon = strchr((const char*)chunk, ':');
    if(colon) {
      temperature_value = atof(colon + 1);
    }
  }
}

/*---------------------------------------------------------------------------*/
static void
mqtt_event(struct mqtt_connection *m, mqtt_event_t event, void *data)
{
  switch(event) {
  case MQTT_EVENT_CONNECTED: {
    LOG_INFO("Application has a MQTT connection\n");
    state = STATE_CONNECTED;
    break;
  }
  case MQTT_EVENT_DISCONNECTED: {
    LOG_INFO("MQTT Disconnect. Reason %u\n", *((mqtt_event_t *)data));
    state = STATE_DISCONNECTED;
    process_poll(&mqtt_device_process);
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
      LOG_INFO("Application is subscribed to topic successfully\n");
    } else {
      LOG_INFO("Application failed to subscribe to topic (ret code %x)\n", suback_event->return_code);
    }
#else
    LOG_INFO("Application is subscribed to topic successfully\n");
#endif
    break;
  }
  case MQTT_EVENT_UNSUBACK: {
    LOG_INFO("Application is unsubscribed to topic successfully\n");
    break;
  }
  case MQTT_EVENT_PUBACK: {
    LOG_INFO("Publishing complete.\n");
    break;
  }
  default:
    LOG_INFO("Application got a unhandled MQTT event: %i\n", event);
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
// Round-robin turn: 1=light, 2=moisture, 3=pH, 4=temperature
static int turn = 1;

/*---------------------------------------------------------------------------*/

PROCESS(mqtt_device_process, "MQTT Device Process");

PROCESS_THREAD(mqtt_device_process, ev, data){

  PROCESS_BEGIN();
  
  mqtt_status_t status;
  char broker_address[CONFIG_IP_ADDR_STR_LEN];

  LOG_INFO("MQTT device process initialization...\n");

  // Initialize the ClientID as MAC address
  snprintf(client_id, BUFFER_SIZE, "%02x%02x%02x%02x%02x%02x",
                     linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1],
                     linkaddr_node_addr.u8[2], linkaddr_node_addr.u8[5],
                     linkaddr_node_addr.u8[6], linkaddr_node_addr.u8[7]);

  // Broker registration					 
  mqtt_register(&conn, &mqtt_device_process, client_id, mqtt_event, MAX_TCP_SEGMENT_SIZE);
                  
  state=STATE_INIT;
                    
  // Initialize periodic timer to check the status 
  etimer_set(&periodic_timer, STATE_MACHINE_PERIODIC);

  /* Main loop */
  while(1) {

    PROCESS_YIELD();

    if((ev == PROCESS_EVENT_TIMER && data == &periodic_timer) || ev == PROCESS_EVENT_POLL){
                            
          if(state==STATE_INIT){
             if(have_connectivity()==true)  
                 state = STATE_NET_OK;
          } 
          
          if(state == STATE_NET_OK){

              // Connect to MQTT server
              LOG_INFO("Connecting to the MQTT server!\n");
              
              memcpy(broker_address, broker_ip, strlen(broker_ip));
              
              mqtt_connect(&conn, broker_address, DEFAULT_BROKER_PORT,
                           (DEFAULT_PUBLISH_INTERVAL * 3) / CLOCK_SECOND,
                           MQTT_CLEAN_SESSION_ON);
              state = STATE_CONNECTING;
          }
          
          if(state==STATE_CONNECTED){
              if(!grow_light_subscribed){
                  strcpy(sub_topic,"growLight");
                  status = mqtt_subscribe(&conn, NULL, sub_topic, MQTT_QOS_LEVEL_0);
                  LOG_INFO("Subscribing to topic growLight\n");
                  if(status != MQTT_STATUS_OUT_QUEUE_FULL) grow_light_subscribed = true;
              }else if(!irrigation_subscribed){
                  strcpy(sub_topic,"irrigation");
                  status = mqtt_subscribe(&conn, NULL, sub_topic, MQTT_QOS_LEVEL_0);
                  LOG_INFO("Subscribing to topic irrigation\n");
                  if(status != MQTT_STATUS_OUT_QUEUE_FULL) irrigation_subscribed = true;
              }else if(!fertilizer_subscribed){
                  strcpy(sub_topic,"fertilizerDispenser");
                  status = mqtt_subscribe(&conn, NULL, sub_topic, MQTT_QOS_LEVEL_0);
                  LOG_INFO("Subscribing to topic fertilizerDispenser\n");
                  if(status != MQTT_STATUS_OUT_QUEUE_FULL) fertilizer_subscribed = true;
              }else if(!fan_subscribed){
                  strcpy(sub_topic,"fan");
                  status = mqtt_subscribe(&conn, NULL, sub_topic, MQTT_QOS_LEVEL_0);
                  LOG_INFO("Subscribing to topic fan\n");
                  if(status != MQTT_STATUS_OUT_QUEUE_FULL) fan_subscribed = true;
              }else if(!heater_subscribed){
                  strcpy(sub_topic,"heater");
                  status = mqtt_subscribe(&conn, NULL, sub_topic, MQTT_QOS_LEVEL_0);
                  LOG_INFO("Subscribing to topic heater\n");
                  if(status != MQTT_STATUS_OUT_QUEUE_FULL) heater_subscribed = true;
              }else if(!light_subscribed){
                  strcpy(sub_topic_light,"light");
                  status = mqtt_subscribe(&conn, NULL, sub_topic_light, MQTT_QOS_LEVEL_0);
                  LOG_INFO("Subscribing to topic light (sensor)\n");
                  if(status != MQTT_STATUS_OUT_QUEUE_FULL) light_subscribed = true;
              }else if(!moisture_subscribed){
                  strcpy(sub_topic_moisture,"soilMoisture");
                  status = mqtt_subscribe(&conn, NULL, sub_topic_moisture, MQTT_QOS_LEVEL_0);
                  LOG_INFO("Subscribing to topic soilMoisture (sensor)\n");
                  if(status != MQTT_STATUS_OUT_QUEUE_FULL) moisture_subscribed = true;
              }else if(!ph_subscribed){
                  strcpy(sub_topic_ph,"pH");
                  status = mqtt_subscribe(&conn, NULL, sub_topic_ph, MQTT_QOS_LEVEL_0);
                  LOG_INFO("Subscribing to topic pH (sensor)\n");
                  if(status != MQTT_STATUS_OUT_QUEUE_FULL) ph_subscribed = true;
              }else if(!temp_subscribed){
                  strcpy(sub_topic_temp,"temperature");
                  status = mqtt_subscribe(&conn, NULL, sub_topic_temp, MQTT_QOS_LEVEL_0);
                  LOG_INFO("Subscribing to topic temperature (sensor)\n");
                  if(status != MQTT_STATUS_OUT_QUEUE_FULL) temp_subscribed = true;
              }else if(grow_light_subscribed && irrigation_subscribed && fertilizer_subscribed &&
                       fan_subscribed && heater_subscribed && light_subscribed && moisture_subscribed &&
                       ph_subscribed && temp_subscribed){
                  LOG_INFO("Successfully subscribed to all topics!\n");
                  state = STATE_SUBSCRIBED;
                  etimer_set(&periodic_timer, SHORT_PUBLISH_INTERVAL);
              }
          }

        if(state == STATE_SUBSCRIBED){
            rgb_led_set(RGB_LED_GREEN);
            if(turn == 1){
                // Use the received light value (do not publish, just log)
                LOG_INFO("Current light value (from sensor): %.2f\n", light_value);
                turn = 2;
            }else if(turn == 2){
                // Use the received soil moisture value (do not publish, just log)
                LOG_INFO("Current soilMoisture value (from sensor): %.2f\n", moisture_value);
                turn = 3;
            }else if(turn == 3){
                // Use the received pH value (do not publish, just log)
                LOG_INFO("Current pH value (from sensor): %.2f\n", pH_value);
                turn = 4;
            }else if(turn == 4){
                // Use the received temperature value (do not publish, just log)
                LOG_INFO("Current temperature value (from sensor): %.2f\n", temperature_value);
                turn = 1;
            }
            etimer_set(&periodic_timer, SHORT_PUBLISH_INTERVAL);
            rgb_led_off();
        } else if ( state == STATE_DISCONNECTED ){
           LOG_ERR("Disconnected from MQTT broker\n");	
           state = STATE_INIT;
        }
        
        if(state != STATE_SUBSCRIBED){
            etimer_set(&periodic_timer, STATE_MACHINE_PERIODIC);
        }

    }//end event check

  }//end while

  PROCESS_END();
  }
