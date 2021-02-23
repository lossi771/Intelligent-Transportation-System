#include "random.h"
#include "stdio.h"
#include "contiki.h"
#include "net/rime/rime.h"
#include "dev/button-sensor.h"
#include "stdlib.h"
#include "dev/leds.h"
#include "string.h"
#include "dev/sht11/sht11-sensor.h"

#define START_TOGGLING 1
#define MAX_RETRANSMISSIONS 5
#define FULL_BATTERY 100

typedef enum {false, true} bool;
typedef struct {
	int temperature;
	int humidity;
} sample_t;

int cross_situation[2]={2,2}; //first element main street, second element second street: 0=emergency, 1=normal, 2=none
int waiting_vehicles=0;
bool crossing_vehicle=false;

int battery_level = FULL_BATTERY;

static struct runicast_conn runicast;
static struct broadcast_conn broadcast;

PROCESS(tl1_control_process, "TL1_CONTROL_PROCESS");
AUTOSTART_PROCESSES(&tl1_control_process);

int get_vehicle_type(char* type){
	if(strcmp(type,"emergency")==0)
		return 0;
	else
		return 1;
}

static void recv_runicast(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno) {}
static void sent_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions){}
static void timedout_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions){}
static void broadcast_sent(struct broadcast_conn *c, int status, int num_tx){}

static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from){
	int type_vehicle = get_vehicle_type((char*)packetbuf_dataptr());
	if(from->u8[0]==37){ //from main street G1
	//if(from->u8[0]==1){//G1**********************************************************************************************
		if(cross_situation[0]==2){
			cross_situation[0]=type_vehicle;
			waiting_vehicles++;
		}
	}
	else{ //from secondary street G2
		if(cross_situation[1]==2){
			cross_situation[1]=type_vehicle;
			waiting_vehicles++;
		}
	}
	printf("broadcast message received from %d.%d: '%s'\n", from->u8[0], from->u8[1], (char *)packetbuf_dataptr());
}

static const struct broadcast_callbacks broadcast_call = {broadcast_recv, broadcast_sent};
static const struct runicast_callbacks runicast_calls = {recv_runicast, sent_runicast, timedout_runicast};

static void close_all() {
	runicast_close(&runicast);
	broadcast_close(&broadcast);
}

void change_tl_status(){
	if((cross_situation[0]==2 && cross_situation[1]!=2) || (cross_situation[0]==1 && cross_situation[1]==0)){
		if((leds_get() & LEDS_RED) == 0){
			leds_on(LEDS_RED);
			leds_off(LEDS_GREEN);
			battery_level = (battery_level-5 < 0)? 0: battery_level-5;
		}
	}
	else{
		if((leds_get() & LEDS_GREEN) == 0){
			leds_on(LEDS_GREEN);
			leds_off(LEDS_RED);
			battery_level = (battery_level-5 < 0)? 0: battery_level-5;
		}
	}
}

int check_cross_status(){
	if((cross_situation[0]==2 && cross_situation[1]!=2) || (cross_situation[0]==1 && cross_situation[1]==0))
		return 1;
	return 0;
}


PROCESS_THREAD(tl1_control_process, ev, data) {

	static struct etimer toggle_timer;
	static struct etimer crossing_timer;
	static struct etimer sensing_timer;
	static struct etimer battery_blinking_timer;

	static bool first_toggle=true;
	linkaddr_t g1;
	g1.u8[0]=37;
	//g1.u8[0]=1;//****************************************************************************************************************************
	g1.u8[1]=0;


	PROCESS_EXITHANDLER(close_all());
	PROCESS_BEGIN();

	SENSORS_ACTIVATE(button_sensor);
	leds_on(LEDS_GREEN);

	etimer_set(&toggle_timer,CLOCK_SECOND*1);
	etimer_set(&sensing_timer,CLOCK_SECOND*5);
	etimer_set(&battery_blinking_timer,CLOCK_SECOND);

	runicast_open(&runicast, 144, &runicast_calls);
	broadcast_open(&broadcast, 129, &broadcast_call);

	while(true) {
			PROCESS_WAIT_EVENT();

			if(ev == sensors_event && data == &button_sensor){
				battery_level=FULL_BATTERY;
				etimer_set(&sensing_timer,CLOCK_SECOND*5);
				//continue;
			}

			if(etimer_expired(&sensing_timer)){
				SENSORS_ACTIVATE(sht11_sensor);
				int temperature = (sht11_sensor.value(SHT11_SENSOR_TEMP)/10-396)/10;
				int humidity = sht11_sensor.value(SHT11_SENSOR_HUMIDITY)/41;
				SENSORS_DEACTIVATE(sht11_sensor);
				sample_t s;
				s.temperature = temperature;
				s.humidity = humidity;
				if(battery_level<=20)	{
					etimer_set(&sensing_timer,CLOCK_SECOND*20);
				}
				else if(battery_level<=50){
					etimer_set(&sensing_timer,CLOCK_SECOND*10);
				}
				else
					etimer_set(&sensing_timer,CLOCK_SECOND*5);
				battery_level = (battery_level-10<0) ? 0: battery_level-10;
				packetbuf_copyfrom(&s,sizeof(sample_t));
				if(!runicast_is_transmitting(&runicast)) {
					runicast_send(&runicast, &g1, MAX_RETRANSMISSIONS);
				}
			}

			if(etimer_expired(&battery_blinking_timer)){
				if(battery_level<=20)
					leds_toggle(LEDS_BLUE);
				else
					leds_off(LEDS_BLUE);
				etimer_reset(&battery_blinking_timer);


				if(etimer_expired(&crossing_timer) && crossing_vehicle==true){ //true discrimina il primo accesso
					waiting_vehicles--;
					if(waiting_vehicles==0){
						crossing_vehicle=false;
						etimer_reset(&toggle_timer);
					}
					else{
						int cts = check_cross_status();
						change_tl_status();
						cross_situation[cts]=2;
						etimer_reset(&crossing_timer);
					}
				}

				if(etimer_expired(&toggle_timer) && crossing_vehicle==false) {
					if(first_toggle==true){
						linkaddr_t tl2;
						tl2.u8[0]=2;//******************************************************************************************************************
						tl2.u8[1]=0;
						packetbuf_copyfrom(&waiting_vehicles,sizeof(int));
						runicast_send(&runicast, &tl2, MAX_RETRANSMISSIONS);
						first_toggle=false;
						etimer_reset(&toggle_timer);
					}
					else{ //non è primo toggle
						if(waiting_vehicles==0){ //both the streets are none
							leds_toggle(LEDS_RED);
							leds_toggle(LEDS_GREEN);
							battery_level = (battery_level-5 < 0)? 0: battery_level-5;
							etimer_reset(&toggle_timer);
						}
						else{
							int cts = check_cross_status();
							change_tl_status();
							crossing_vehicle=true;
							cross_situation[cts]=2;
							etimer_set(&crossing_timer,CLOCK_SECOND*5);
						}
					}
				}
			}//bb
	}
	PROCESS_END();
}
