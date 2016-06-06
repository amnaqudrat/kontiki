/*
 * Copyright (c) 2013, Institute for Pervasive Computing, ETH Zurich
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 */

/**
 * \file
 *      Example resource
 * \author
 *      Matthias Kovatsch <kovatsch@inf.ethz.ch>
 */

#include <string.h>
#include "rest-engine.h"
#include "er-coap.h"

//------- prediction custom libs ------
#include "dev/adxl345.h"
#include <math.h>
#include "dev/leds.h"
//------- End prediction custom libs ------

//------- prediction functions ------
// set the sensor reading value interval
#define ACCM_READ_INTERVAL    CLOCK_SECOND/50

// if you want "STANDING" instead of "1" just set it to 1
#define CHAR_STR_STATUS 1 
#if CHAR_STR_STATUS
  static char *STANDING   = "STANDING";  //STANDING
  static char *WALKING    = "WALKING";   //WALKING
  static char *RUNNING    = "RUNNING";   //RUNNING
  static char *FALLING    = "FALLING";   //FALLING
  static char *STATUS_PT  =  NULL;       //Nothing
  static char *status_str = "STANDING";  //STANDING
  static char last;
#else
  static char *STANDING   = "1"; //STANDING
  static char *WALKING    = "2"; //WALKING
  static char *RUNNING    = "3"; //RUNNING
  static char *FALLING    = "4"; //FALLING
  static char *STATUS_PT  = NULL;//Nothing
  static char *status_str = "1"; //STANDING
  static char last;
#endif

// declare/define the pridiction function.
void predict();
//	viola ! these are actions to be fired on each event. 
//	i.e set status 1, print walking, turn on blue LED and off other LEDs when WALKING is fired.
void standing(){ notify(); status_str = STANDING; printf("standing\n"); leds_on(LEDS_BLUE);  leds_off(LEDS_RED); leds_off(LEDS_GREEN); }
void walking() { notify(); status_str = WALKING; printf("walking\n");  leds_on(LEDS_GREEN); leds_off(LEDS_RED); leds_off(LEDS_BLUE);  }
void running() { notify(); status_str = RUNNING; printf("running\n");  leds_on(LEDS_GREEN); leds_on(LEDS_RED);  leds_off(LEDS_BLUE);  }
void falling() { notify(); status_str = FALLING; printf("falling\n");  leds_on(LEDS_RED);   leds_off(LEDS_BLUE);leds_off(LEDS_GREEN); }

#define HISTORY 16
#define sampleNo 70
// Why I'm using int16_t ? (finally I discovered, I don't know)
/* c Type   |stdint.h Type|Bits|Signed| Range           |
 signed char| int16_t     | 16 |Signed|-32,768 .. 32,767| */

static int16_t x,y,z;
static int16_t sample[3][sampleNo];
static int16_t pos;

static struct etimer et;

/* accelerometer free fall detection callback */
void accm_ff_cb(uint8_t reg){
#define ANNOYING_ALWAYS_THERE_ANYWAY_OUTPUT 0
  if(reg && ADXL345_INT_FREEFALL){last=*STATUS_PT;STATUS_PT=FALLING;if(last!=*STATUS_PT){ notify(); falling(); }}
}

//-------End prediction functions ------

//---End Function Deffinitions e-MCH-APp ---

static void res_get_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
static void res_periodic_handler(void);

PERIODIC_RESOURCE(res_z1_coap_obs_rtgs,
                  "title=\"rTGS\";obs",
                  res_get_handler,
                  NULL,
                  NULL,
                  NULL,
                  5 * CLOCK_SECOND,
                  res_periodic_handler);

/*
 * Use local resource state that is accessed by res_get_handler() and altered by res_periodic_handler() or PUT or POST.
 */
static int32_t event_counter = 0;

static void
res_get_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  /*
   * For minimal complexity, request query and options should be ignored for GET on observable resources.
   * Otherwise the requests must be stored with the observer list and passed by REST.notify_subscribers().
   * This would be a TODO in the corresponding files in contiki/apps/erbium/!
   */
  REST.set_header_content_type(response, REST.type.TEXT_PLAIN);
  REST.set_header_max_age(response, res_z1_coap_obs_rtgs.periodic->period / CLOCK_SECOND);
  REST.set_response_payload(response, buffer, snprintf((char *)buffer, preferred_size, "%s", STATUS_PT));

  /* The REST.subscription_handler() will be called for observable resources by the REST framework. */
}
  /* Usually a condition is defined under with subscribers are notified, e.g., large enough delta in sensor reading. */
void notify() {
    /* Notify the registered observers which will trigger the res_get_handler to create the response. */
    REST.notify_subscribers(&res_z1_coap_obs_rtgs);
}
/*
 * Additionally, a handler function named [resource name]_handler must be implemented for each PERIODIC_RESOURCE.
 * It will be called by the REST manager process with the defined period.
 */
static void
res_periodic_handler()
{
  /* Do a periodic task here, e.g., sampling a sensor. */
  //----------- Init ADXL Sensor ------------
  ACCM_REGISTER_INT1_CB(accm_ff_cb);
  //-----------End Init ADXL Sensor ------------
  printf("Motion Tracking Started\n");
    //------------ Prediction (read values) ------------------
    x = accm_read_axis(X_AXIS);
    y = accm_read_axis(Y_AXIS);
    z = accm_read_axis(Z_AXIS);

    //printf("%d : %d : %d \n", x, y, z);
    sample[0][pos]=(int16_t)x;
    sample[1][pos]=(int16_t)y;
    sample[2][pos]=(int16_t)z;

    if(pos==(sampleNo-1))predict();
    pos=(pos+1)%sampleNo;
    etimer_set(&et, ACCM_READ_INTERVAL);
    while(etimer_expired(&et));
    if(STATUS_PT==FALLING){
      etimer_set(&et, 15);
      while(etimer_expired(&et));}
    //------------ End Prediction (read values) ------------------
    //printf("FinalStatus: %s\n", status_str);

  printf("Motion Tracking Terminated\n");
  ++event_counter;
}

/*--- - Human Body Posture Detection and Prediction ---------------*/
void predict(){
  double mag[sampleNo-1],sum=0,max=0,std=0,mean=0,tmp=0;
  double result1,result2,result3,result4;
  int i;
  for(i=1;i<sampleNo;i++){
    mag[i-1]=sqrt(pow(sample[0][i]-sample[0][i-1],2)+pow(sample[1][i]-sample[1][i-1],2)+pow(sample[2][i]-sample[2][i-1],2));
    sum=sum+mag[i-1];
    if(mag[i-1]>max)max=mag[i-1];
  }

  mean =(sum/sampleNo);
  for(i=0; i<sampleNo-1; i++){ tmp = tmp + pow( ( 1 - mean ), 2); }
  std=sqrt(tmp/80);
  //printf("This is data from predict:%d,%d\n",(int)std,(int)mean);
  if(std>1000)return;
  double std1=3, mean1=2.2, max1=0.7;
  double std2=30, mean2=37, max2=5.74;
  double std3=68, mean3=83, max3=22;
  double std4=96, mean4=34, max4=42;
  double a1=1,a2=1,a3=0;
  result1 = sqrt(a1*pow((std-std1),2)+a2*pow((mean-mean1),2)+a3*pow((max-max1),2));
  result2 = sqrt(a1*pow((std-std2),2)+a2*pow((mean-mean2),2)+a3*pow((max-max2),2));
  result3 = sqrt(a1*pow((std-std3),2)+a2*pow((mean-mean3),2)+a3*pow((max-max3),2));
  result4 = sqrt(a1*pow((std-std4),2)+a2*pow((mean-mean4),2)+a3*pow((max-max4),2));
  //printf("This is four results:%d,%d,%d,%d\n",(int)result1,(int)result2,(int)result3,(int)result4);
  if(result1<result2 && result1<result3 && result1<result4){ last=*STATUS_PT;STATUS_PT=STANDING; if(last!=*STATUS_PT){ standing();} }
  if(result2<result1 && result2<result3 && result2<result4){ last=*STATUS_PT;STATUS_PT=WALKING;  if(last!=*STATUS_PT){ walking(); } }
  if(result3<result1 && result3<result2 && result3<result4){ last=*STATUS_PT;STATUS_PT=RUNNING;  if(last!=*STATUS_PT){ running(); } }
}
/*---- End Human Body Posture Detection and Prediction ---------------*/