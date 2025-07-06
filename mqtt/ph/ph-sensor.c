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
/*---------------------------------------------------------------------------*/
#define LOG_MODULE "pH sensor"
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
PROCESS_NAME(mqtt_pH_process);
AUTOSTART_PROCESSES(&mqtt_pH_process);

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
PROCESS(mqtt_pH_process, "mqtt pH client");

/*---------------------------------------------------------------------------*/

// variable to implement correctly the simulation, related to the actuator in the coap network
static int fertilizer_erogation_variation = 0;

static void
pub_handler(const char *topic, uint16_t topic_len, const uint8_t *chunk,
            uint16_t chunk_len)
{

  if(strcmp(topic, "fertilizerDispenser") == 0) {
    
  
    if(strcmp((const char*) chunk, "OFF") == 0) { // no change in fertilizer, random behavior
      fertilizer_erogation_variation = 0;
    } else if(strcmp((const char*) chunk, "SDEC") == 0) { // soft decrease fertilizer to increase pH slowly
      fertilizer_erogation_variation = -1;
    } else if(strcmp((const char*) chunk, "SINC") == 0)  { // soft increase fertilizer to decrease pH slowly
      fertilizer_erogation_variation = 1;
    } else if(strcmp((const char*) chunk, "DEC") == 0) { // decrease fertilizer to increase pH
      fertilizer_erogation_variation = -2;
    } else if(strcmp((const char*) chunk, "INC") == 0)  { // increase fertilizer to decrease pH
      fertilizer_erogation_variation = 2;
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
    process_poll(&mqtt_pH_process);
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
      LOG_INFO("application is subscribed to topic successfully\n");
    } else {
      LOG_INFO("application failed to subscribe to topic (ret code %x)\n", suback_event->return_code);
    }
#else
    LOG_INFO("application is subscribed to topic successfully\n");
#endif
    break;
  }
  case MQTT_EVENT_UNSUBACK: {
    LOG_INFO("application is unsubscribed to topic successfully\n");
    break;
  }
  case MQTT_EVENT_PUBACK: {
    LOG_INFO("publishing complete.\n");
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
/*----------------------------------simulation-------------------------------*/
/*---------------------------------------------------------------------------*/


/*initialized the value of the pH to the value at the center of the interval*/
static float pH_value = 6.75;

/*values used respectively to define the upper bound of the possible variation interval and for the standard pH
  variation in case of stabilization using fertilizer*/
static float max_pH_variation = 0.05;
static float pH_variation_fertilizer = 0.1;
static float soft_pH_variation_fertilizer = 0.05;

/*the following function simulates changes of the pH sensed by the sensor; parameter controls fertilizer effect*/
static void change_pH_simulation(){

  
  /*if no change in fertilizer is active, random behaviour*/
  if(fertilizer_erogation_variation == 0){
    /*generate an integer from {0,1,2} to decide simulation*/
    int decision = rand() % 3;

    /*generate a float in [0.0, 0.05] for pH variation*/
    float pH_variation = (float)rand()/(float)(RAND_MAX/max_pH_variation);

    switch(decision){
      case 0:{
        break;
      }
      case 1:{
        pH_value += pH_variation;
        break;
      }
      case 2:{
        pH_value -= pH_variation;
        break;
      }      
    }

  /*the fertilizer tries to reduce or increase pH gradually*/
  }else if(fertilizer_erogation_variation == -1){
    pH_value += soft_pH_variation_fertilizer; // soft increase in fertilizer to softly decrease pH

  }else if(fertilizer_erogation_variation == -2){ 
    pH_value += pH_variation_fertilizer; // increase fertilizer to decrease pH

  }else if(fertilizer_erogation_variation == 1){
    pH_value -= soft_pH_variation_fertilizer; // soft decrease in fertilizer to softly increase pH

  }else if(fertilizer_erogation_variation == 2){
    pH_value -= pH_variation_fertilizer; // decrease fertilizer to increase pH
  }
}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/

PROCESS_THREAD(mqtt_pH_process, ev, data)
{

  PROCESS_BEGIN();
  
  mqtt_status_t status;
  char broker_address[CONFIG_IP_ADDR_STR_LEN];

  LOG_INFO("mqtt pH process\n");

  // initialize the clientID as MAC address
  snprintf(client_id, BUFFER_SIZE, "%02x%02x%02x%02x%02x%02x",
                     linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1],
                     linkaddr_node_addr.u8[2], linkaddr_node_addr.u8[5],
                     linkaddr_node_addr.u8[6], linkaddr_node_addr.u8[7]);

  // broker registration           
  mqtt_register(&conn, &mqtt_pH_process, client_id, mqtt_event,
                  MAX_TCP_SEGMENT_SIZE);
          
  state=STATE_INIT;
            
  // initialize periodic timer to check the status 
  etimer_set(&periodic_timer, STATE_MACHINE_PERIODIC);

  /* main loop */
  while(1) {

    PROCESS_YIELD();

    if((ev == PROCESS_EVENT_TIMER && data == &periodic_timer) || 
          ev == PROCESS_EVENT_POLL){
              
          if(state==STATE_INIT){
             if(have_connectivity()==true)  
                 state = STATE_NET_OK;
          } 
          
          if(state == STATE_NET_OK){
              // connect to mqtt server
              LOG_INFO("connecting to the mqtt server!\n");
              
              memcpy(broker_address, broker_ip, strlen(broker_ip));
              
              mqtt_connect(&conn, broker_address, DEFAULT_BROKER_PORT,
                           (DEFAULT_PUBLISH_INTERVAL * 3) / CLOCK_SECOND,
                           MQTT_CLEAN_SESSION_ON);
              state = STATE_CONNECTING;
          }
          
          if(state==STATE_CONNECTED){
          
              // subscribe to a topic
              strcpy(sub_topic,"fertilizerDispenser");

              status = mqtt_subscribe(&conn, NULL, sub_topic, MQTT_QOS_LEVEL_0);

              LOG_INFO("subscribing to topic fertilizerDispenser for simulation purposes!\n");
              if(status == MQTT_STATUS_OUT_QUEUE_FULL) {
                LOG_ERR("tried to subscribe but command queue was full!\n");
                PROCESS_EXIT();
              }
              
              state = STATE_SUBSCRIBED;
          }

              
        if(state == STATE_SUBSCRIBED){
          // publish pH sensor value
          sprintf(pub_topic, "%s", "pH");
          
          change_pH_simulation();

          // send pH with two decimals
          sprintf(app_buffer, "{\"pH\":%.2f}", pH_value);
          
          
          mqtt_publish(&conn, NULL, pub_topic, (uint8_t *)app_buffer,
               strlen(app_buffer), MQTT_QOS_LEVEL_0, MQTT_RETAIN_OFF);
        
        } else if ( state == STATE_DISCONNECTED ){
           LOG_ERR("disconnected form mqtt broker\n"); 
           state = STATE_INIT;
        }
        
        etimer_set(&periodic_timer, SHORT_PUBLISH_INTERVAL);
      
    }

  }

  PROCESS_END();
}
