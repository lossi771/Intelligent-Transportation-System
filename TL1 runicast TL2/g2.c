#include "stdio.h"
#include "contiki.h"
#include "net/rime/rime.h"
#include "dev/button-sensor.h"
#include "string.h"
#include "dev/sht11/sht11-sensor.h"

#define MAX_RETRANSMISSIONS 5

typedef struct {
	int temperature;
	int humidity;
} sample_t;

static struct runicast_conn runicast;
static struct broadcast_conn broadcast;

PROCESS(G2_control_process, "G2_CONTROL_PROCESS");
AUTOSTART_PROCESSES(&G2_control_process);

static void recv_runicast(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno) {}
static void sent_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions){}
static void timedout_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions){}

static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from){}
static void broadcast_sent(struct broadcast_conn *c, int status, int num_tx){}

static const struct runicast_callbacks runicast_calls = {recv_runicast, sent_runicast, timedout_runicast};
static const struct broadcast_callbacks broadcast_calls = {broadcast_recv, broadcast_sent};

static void close_connections(){
	runicast_close(&runicast);
	broadcast_close(&broadcast);
}

PROCESS_THREAD(G2_control_process, ev, data) {
	static struct etimer second_pression_timer;
	static struct etimer sensing_timer;

	linkaddr_t g1;
	g1.u8[0]=37;
	//g1.u8[0]=1;****************************************************************************************************************************
	g1.u8[1]=0;


	PROCESS_EXITHANDLER(close_connections());
	PROCESS_BEGIN();

	SENSORS_ACTIVATE(button_sensor);
	SENSORS_ACTIVATE(sht11_sensor);

	runicast_open(&runicast, 144, &runicast_calls);
	broadcast_open(&broadcast, 129, &broadcast_calls);

	etimer_set(&sensing_timer,CLOCK_SECOND*5); //sense both variables every 5 seconds

	while(1) {
		PROCESS_WAIT_EVENT();
		//case 1: button pression from a vehicle
		if(ev == sensors_event && data == &button_sensor) {
			etimer_set(&second_pression_timer,CLOCK_SECOND*1); //poi nel caso fai la define per renderlo più elegante
			PROCESS_WAIT_EVENT();
			if(ev == sensors_event && data == &button_sensor) { //emergency vehicle
				packetbuf_copyfrom("emergency",strlen("emergency")*sizeof(char)+1); //carattere di fine stringa
				broadcast_send(&broadcast);
				etimer_stop(&second_pression_timer); //timer stopped in order to avoid the expiration event
		    }
			else { //normal vehicle
				packetbuf_copyfrom("normal",strlen("normal")*sizeof(char)+1); //carattere di fine stringa
				broadcast_send(&broadcast);
			}
		}

		//case 2: sensing_timer expiration
		if(etimer_expired(&sensing_timer)){
			SENSORS_ACTIVATE(sht11_sensor);
			int temp = (sht11_sensor.value(SHT11_SENSOR_TEMP)/10-396)/10;
			int hum = sht11_sensor.value(SHT11_SENSOR_HUMIDITY)/41;
			sample_t s;
			s.temperature = temp;
			s.humidity = hum;
			packetbuf_copyfrom(&s,sizeof(sample_t));

			if(!runicast_is_transmitting(&runicast)) {
				runicast_send(&runicast, &g1, MAX_RETRANSMISSIONS);
			}
			SENSORS_DEACTIVATE(sht11_sensor);
			etimer_reset(&sensing_timer);
		}
	}
	PROCESS_END();
}

