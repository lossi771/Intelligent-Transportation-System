#include "stdio.h"
#include "stdlib.h"
#include "contiki.h"
#include "net/rime/rime.h"
#include "dev/button-sensor.h"
#include "string.h"
#include "dev/sht11/sht11-sensor.h"
#include "dev/serial-line.h"

#define SAMPLE_RECEIVED_EVENT	35	// Custom Event

typedef enum { false, true} bool;
typedef struct {
	int temperature;
	int humidity;
} sample_t;

static struct runicast_conn runicast;
static struct broadcast_conn broadcast;

PROCESS(keyboard_process, "KEYBOARD_PROCESS");
PROCESS(g1_control_process, "G1_CONTROL_PROCESS");

AUTOSTART_PROCESSES(&g1_control_process, &keyboard_process);

//buffer to maintain samples from other sensors
int temp_samples[3];
int hum_samples[3];
//variables for averages
int temp_avg;
int hum_avg;
//number of temperatures/humidities inserted
int num_samples_ins=0;
//array to see which sample is not been received yet
bool sample_inserted[3]={false, false, false};


//RTD
char* warning_string = 0;
static void display_RTD() {
	char* displayed_warning = (warning_string!=0)?warning_string:"";
	printf("Display TRD:\n");
	printf("%s + %d + %d\n", displayed_warning,temp_avg, hum_avg);
	warning_string="";
}

int get_array_index(unsigned int addr){
	switch(addr){
		case 45: return 0; // G2 index 0
		case 1:	return 1; //TL1 index 1
		case 2: return 2; //TL2 index 2
		default: return -1;
	}
}


static void insert_sample(sample_t sample,const linkaddr_t* sender) {
	int index=get_array_index(sender->u8[0]);
	temp_samples[index] = sample.temperature;
	hum_samples[index] = sample.humidity;
	if(sample_inserted[index]==false) {
		num_samples_ins++;
		sample_inserted[index] = true;
	}
}

//method to compute averages and reset structures
static void compute_avgs(int sink_tmp, int sink_hum) {
	temp_avg = (sink_tmp+temp_samples[0]+temp_samples[1]+temp_samples[2])/4;
	hum_avg = (sink_hum+hum_samples[0]+hum_samples[1]+hum_samples[2])/4;
	num_samples_ins = 0;
	sample_inserted[0] = sample_inserted[1] = sample_inserted[2] = false;
}

//CALLBACKS RUNICAST
//sample receiving
static void recv_runicast(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno) {
	sample_t* sample = (sample_t*)packetbuf_dataptr();
	insert_sample(*sample,from);
	process_post(&g1_control_process,SAMPLE_RECEIVED_EVENT,packetbuf_dataptr());
}

static void sent_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions){}
static void timedout_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions){}

//CALLBACKS BROADCAST
static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from){}
static void broadcast_sent(struct broadcast_conn *c, int status, int num_tx){}

//CALLBACKS STRUCT
static const struct runicast_callbacks runicast_calls = {recv_runicast, sent_runicast, timedout_runicast};
static const struct broadcast_callbacks broadcast_calls = {broadcast_recv, broadcast_sent};

static void close_connections() {
	runicast_close(&runicast);
	broadcast_close(&broadcast);
}

//PROCESS TO VERIFY VEHICLES AND TO HANDLE DATA FROM OTHER SENSORS
PROCESS_THREAD(g1_control_process, ev, data) {
	static struct etimer second_pression_timer;

	PROCESS_EXITHANDLER(close_connections());
	PROCESS_BEGIN();

	SENSORS_ACTIVATE(button_sensor);

	runicast_open(&runicast, 144, &runicast_calls);
	broadcast_open(&broadcast, 129, &broadcast_calls);

	while(1) {
		PROCESS_WAIT_EVENT();
		//case 1: button pressure from a vehicle
		if(ev == sensors_event && data == &button_sensor) {
			etimer_set(&second_pression_timer,CLOCK_SECOND*1);
			PROCESS_WAIT_EVENT();
			if(ev == sensors_event && data == &button_sensor) { //emergency vehicle
				packetbuf_copyfrom("emergency",strlen("emergency")*sizeof(char)+1);
				broadcast_send(&broadcast);
				etimer_stop(&second_pression_timer); //timer stopped in order to avoid the expiration event
		    }
			else { //normal vehicle
				packetbuf_copyfrom("normal",strlen("normal")*sizeof(char)+1);
				broadcast_send(&broadcast);
			}
		}
		//case 2: sample received in runicast
		else if(ev == SAMPLE_RECEIVED_EVENT) {
			if(num_samples_ins==3){
				SENSORS_ACTIVATE(sht11_sensor);
				int tmp = (sht11_sensor.value(SHT11_SENSOR_TEMP)/10-396)/10;
				int hum = sht11_sensor.value(SHT11_SENSOR_HUMIDITY)/41;
				compute_avgs(tmp, hum);
				display_RTD();
				SENSORS_DEACTIVATE(sht11_sensor);
			}
		}
	}
	PROCESS_END();
}

//PROCESS TO HANDLE KEYBOARD
PROCESS_THREAD(keyboard_process, ev, data) {
	bool pass_ok = false;
	PROCESS_BEGIN();
	serial_line_init();
	while(1){
		while(pass_ok == false){
			printf("\nEnter the password please :\n");
			PROCESS_WAIT_EVENT_UNTIL(ev==serial_line_event_message);
			if(strcmp((char *)data, "NES") != 0){
				printf("Incorrect password.\n");
			} else {
				pass_ok = true;
			}
		}
		printf("Password ok, type the emergency warning :\n");
		PROCESS_WAIT_EVENT_UNTIL(ev==serial_line_event_message);
		char* typed_warning = (char*)data;
		warning_string = malloc(sizeof(char)*(strlen(typed_warning)+1));
		strcpy(warning_string,typed_warning);
		pass_ok = false;
	}
	PROCESS_END();
}
