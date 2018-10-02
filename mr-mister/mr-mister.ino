/*
  mr-mister.ino - Mr. Mister irrigation control software for LCTECH ESP boards

  Copyright (C) 2018  Bryce Barbato and Jim Donelson

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <ESP8266WiFi.h>
#include <time.h>                       // time() ctime()
#include <sys/time.h>                   // struct timeval
#include <coredecls.h>                  // settimeofday_cb()
#include <Dusk2Dawn.h>                  // calculates sunrise/sunset
#include "mmr_version.h"                // version information

#define RELAY_ON    1
#define RELAY_OFF   0

//#define TEST_TIME
#define TEST_CHAN
#define TCPPRINT

//Since console and relay control both use the UART,
//pick only one of the two options below.  In order to
//debug, uncomment PRINT.  To run, uncomment RELAY_CMD
//#define RELAY_CMD
#define PRINT_SERIAL

#ifdef TCPPRINT
  #define LOGPRINTF(x)  serverClient.print (x)
  #define LOGPRINTLN(x)  serverClient.println (x)
#else
  #define LOGPRINTF(x)  Serial.print (x)
  #define LOGPRINTLN(x)  Serial.println (x)
#endif

#ifdef PRINT_SERIAL
 #define PRINTF(x)  Serial.print (x)
 #define PRINTLN(x)  Serial.println (x)
#else
 #define PRINTF(x)
 #define PRINTLN(x)
#endif

#ifdef RELAY_CMD
 #define SERIAL_CMD(x)  Serial.write (x)
 #define SERIAL_FLUSH(x)   Serial.flush (x)
#else
 #define SERIAL_CMD(x)
 #define SERIAL_FLUSH(x)
#endif

////////////////////////////////////////////////////////
//// ONE TIME CONFIGURATION PARAMETERS /////////////////

#define SSID            "SSID"
#define SSIDPWD         "Password"
#define TZ              -5          // (utc+) TZ in hours
#define DST_MN          60          // use 60mn for summer time in some countries
#define LAT             26.843501   // Local latitude for sun calculations
#define LON             -80.082038  // Local longitude for sun calculations
#define MORN_DELAY_S    0           // Time to wait after sunrise
#define NUM_RELAYS      4      

////////////////////////////////////////////////////////
//// TO BE CONVERTED TO RUNTIME ////////////////////////

#define CHAN_1_RUNLENGTH_S  10    //Length of spray
#define CHAN_1_INTERVAL_S   600   //Interval to wait between sprays
#define CHAN_2_RUNSTART_S   -602  //Seconds before sunrise
#define CHAN_2_RUNLENGTH_S  300   //Interval to wait between sprays
#define CHAN_3_RUNSTART_S   -301  //Seconds before sunrise
#define CHAN_3_RUNLENGTH_S  300   //Interval to wait between sprays

////////////////////////////////////////////////////////

#define TZ_MN           ((TZ)*60)
#define TZ_SEC          ((TZ)*3600)
#define DST_SEC         ((DST_MN)*60)

timeval cbtime;      // time set in callback
bool cbtime_set = false;

Dusk2Dawn location(LAT, LON, TZ);

// declare Telnet server (do NOT put in setup())
WiFiServer telnetServer(23);
WiFiClient serverClient;

void time_is_set(void) {
  gettimeofday(&cbtime, NULL);
  cbtime_set = true;
  PRINTLN("------------------ settimeofday() was called ------------------");
}

void setup() {
  
  Serial.begin(115200);
  delay(3000);  //Delay to allow catching AP connection in terminal
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); //Turn LED OFF
  SetAllRelays(RELAY_OFF);

  settimeofday_cb(time_is_set);

  configTime(TZ_SEC, DST_SEC, "pool.ntp.org");
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, SSIDPWD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    PRINTF(".");
  }
  PRINTLN("");
  PRINTF("Connected to ");
  PRINTLN(SSID);
  PRINTF("IP address: ");
  PRINTLN(WiFi.localIP());
  PRINTLN("Setup done");
  // don't wait, observe time changing when ntp timestamp is received

  telnetServer.begin();
  telnetServer.setNoDelay(true);

  delay(20);

  PrintVersion();
}

// for testing purpose:
extern "C" int clock_gettime(clockid_t unused, struct timespec *tp);

#define PTM(w) \
  LOGPRINTF(":" #w "="); \
  LOGPRINTF(tm->tm_##w);

void printTm(const char* what, const tm* tm) {
  PRINTF(what);
  PTM(isdst); PTM(yday); PTM(wday);
  PTM(year);  PTM(mon);  PTM(mday);
  PTM(hour);  PTM(min);  PTM(sec);
}

void printTmShort(const char* what, const tm* tm) {
  LOGPRINTF(what);
  PTM(year+1900);  PTM(mon+1);  PTM(mday);
  PTM(hour);  PTM(min);  PTM(sec);
}

time_t now, midnight;
time_t sunrise_t, sunset_t;
tm *tm;
uint32_t today_s;
int sunrise_s, sunset_s;
int this_day = -1;
int runstart1_s, starttime_s;
bool chan_1_msg_flag = true, chan_1_time_flag = true;
bool chan_2_msg_flag = true;
bool chan_3_msg_flag = true;
bool five_sec_flag = true;
uint32_t five_sec_timer = 0;

void loop() {

  if (telnetServer.hasClient()) {
    if (!serverClient || !serverClient.connected()) {
      if (serverClient) {
        serverClient.stop();
        Serial.println("Telnet Client Stop");
      }
      serverClient = telnetServer.available();
      Serial.println("New Telnet client");
      serverClient.flush();  // clear input buffer, else you get strange characters 
    }
  }

  while(serverClient.available()) {  // get data from Client
    Serial.write(serverClient.read());
  }
  
  //Read time figures
  now = time(nullptr);
  tm = localtime(&now);

  //Create five second interval timer for display
  if((uint32_t)now > five_sec_timer){
    five_sec_flag = true;
    five_sec_timer = (uint32_t)now + 15;
  }
  
  //Recalculate sunrise/sunset on new day
  if(this_day != tm->tm_yday) {
    uint32_t now_s = (uint32_t)now;
    uint32_t now_hour_s = 3600 * tm->tm_hour;
    uint32_t now_min_s = 60 * tm->tm_min;
    uint32_t now_sec = tm->tm_sec;

    //Calculate midnight to create offset for today
    midnight = (time_t)(now_s - (now_hour_s + now_min_s + now_sec));
    LOGPRINTF("Midnight:");
    LOGPRINTF((uint32_t)midnight);
    printTmShort(" Midnight Time", localtime(&midnight));
    LOGPRINTLN("");

    //Calculate sunrise and sunset
    sunrise_s  = 60 * location.sunrise(tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, true);
    sunset_s   = 60 * location.sunset(tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, true);
    sunrise_t = midnight+(time_t)sunrise_s;
    sunset_t = midnight+(time_t)sunset_s;
    LOGPRINTLN("Recalculated Sunrise/Sunset");
    starttime_s = sunrise_s + MORN_DELAY_S;

    //If new boot, set to current time; else, sunrise + offset
    if(this_day == -1)
      #ifdef TEST_TIME
        runstart1_s = starttime_s;
      #else
        runstart1_s = (uint32_t)now - (uint32_t)midnight;
      #endif
    else
      runstart1_s = starttime_s;
    LOGPRINTF("---- Initial runstart1_s :");
    LOGPRINTLN(runstart1_s);
    this_day = tm->tm_yday;
  }

  //Calculate the number of seconds so far today
  #ifdef TEST_TIME
    today_s = sunrise_s + CHAN_2_RUNSTART_S - 60 + millis()/1000;
  #else
    today_s = (uint32_t)now - (uint32_t)midnight;
  #endif

  //Print human readable time figures every 5 seconds
  if(five_sec_flag){
    tm = localtime(&now);

    LOGPRINTF("Today's:");
    LOGPRINTF(today_s);
    LOGPRINTF(" DoY:");
    LOGPRINTF(tm->tm_yday);
    LOGPRINTF(" DoM:");
    LOGPRINTF(tm->tm_mday);
    LOGPRINTF(" MoY+1:");
    LOGPRINTF(tm->tm_mon+1);
    LOGPRINTF(" Y+1900:");
    LOGPRINTF(tm->tm_year+1900);
    LOGPRINTF(" Time:");
    LOGPRINTF(tm->tm_hour);
    LOGPRINTF(":");
    LOGPRINTF(tm->tm_min);
    LOGPRINTF(":");
    LOGPRINTLN(tm->tm_sec);

    //Print calculated sunrise
    LOGPRINTF("Sunrise:");
    LOGPRINTF(sunrise_s);
    printTmShort("   Sunrise", localtime(&sunrise_t));
    LOGPRINTLN();
    
    //Print calculated sunset
    LOGPRINTF("Sunset :");
    LOGPRINTF(sunset_s);
    printTmShort("    Sunset", localtime(&sunset_t));
    LOGPRINTLN();
  }

  //Guts for running solenoid
  //Should eventually be a class
  if((today_s > sunrise_s) && (today_s < sunset_s))
  {
    if(chan_1_time_flag){
      chan_1_time_flag = false;
      LOGPRINTF("---- Sun is Up! Today's Seconds: ");
      LOGPRINTLN(today_s);
    }

    //Run solenoid 1 during calculated period
    if((today_s >= runstart1_s) && (today_s < (runstart1_s + CHAN_1_RUNLENGTH_S))){
      if(chan_1_msg_flag){
        chan_1_msg_flag = false;
        LOGPRINTF("---- CHAN_1: ON ");
        LOGPRINTLN(today_s);
      }
      SetRelay(1, RELAY_ON);
    }
    else {
      //Calculate new start time for solenoid
      if(today_s >= (runstart1_s + CHAN_1_RUNLENGTH_S)){
        chan_1_msg_flag = true;
        chan_1_time_flag = true;
        runstart1_s += CHAN_1_INTERVAL_S;
        LOGPRINTF("---- CHAN_1: OFF ");
        LOGPRINTLN(today_s);
        LOGPRINTF("---- CHAN_1 NEWSTART: ");
        LOGPRINTLN(runstart1_s);
      }
      SetRelay(1, RELAY_OFF);
    }
  }

  //Run solenoid 2 during calculated period
  if((today_s >= sunrise_s + CHAN_2_RUNSTART_S) && (today_s < (sunrise_s + CHAN_2_RUNSTART_S + CHAN_2_RUNLENGTH_S))){
    if(chan_2_msg_flag){
      chan_2_msg_flag = false;
      LOGPRINTF("---- CHAN_2: ON ");
      LOGPRINTLN(today_s);
    }
    SetRelay(2, RELAY_ON);
  }
  else {
    //Calculate new start time for solenoid
    if(today_s >= (sunrise_s + CHAN_2_RUNSTART_S + CHAN_2_RUNLENGTH_S)){
      if(!chan_2_msg_flag){      
        chan_2_msg_flag = true;
        LOGPRINTF("---- CHAN_2: OFF ");
        LOGPRINTLN(today_s);
      }
    SetRelay(2, RELAY_OFF);
    }
  }
  
  //Run solenoid 3 during calculated period
  if((today_s >= sunrise_s + CHAN_3_RUNSTART_S) && (today_s < (sunrise_s + CHAN_3_RUNSTART_S + CHAN_3_RUNLENGTH_S))){
    if(chan_3_msg_flag){
      chan_3_msg_flag = false;
      LOGPRINTF("---- CHAN_3: ON ");
      LOGPRINTLN(today_s);
    }
    SetRelay(3, RELAY_ON);
  }
  else {
    //Calculate new start time for solenoid
    if(today_s >= (sunrise_s + CHAN_3_RUNSTART_S + CHAN_3_RUNLENGTH_S)){
      if(!chan_3_msg_flag){      
        chan_3_msg_flag = true;
        LOGPRINTF("---- CHAN_3: OFF ");
        LOGPRINTLN(today_s);
      }
    SetRelay(3, RELAY_OFF);
    }
  }

  //Reset flag
  five_sec_flag = false;
}

void SetRelay(char relay_num, char state){

  if((relay_num == 0) || (relay_num > NUM_RELAYS))
    return;
    
  state = state &1;
  if(state)
    digitalWrite(LED_BUILTIN, LOW); //Turn LED ON
  else
    digitalWrite(LED_BUILTIN, HIGH); //Turn LED OFF
  
  SERIAL_CMD(0xA0);
  SERIAL_CMD(relay_num);
  SERIAL_CMD(state);
  SERIAL_CMD(0xA0+relay_num+state);
  SERIAL_FLUSH();
  delay(20);
  
}

void SetAllRelays(char state){

  char i;
  state = state &1;
  
  for(i = 1; i <= NUM_RELAYS; i++){
    SetRelay(i, state);
    delay(20);
  }
}

void PrintVersion(){

  LOGPRINTF("Version: ");
  LOGPRINTF(MMR_MAJOR_V);
  LOGPRINTF(".");
  LOGPRINTF(MMR_MINOR_V);
  LOGPRINTF(".");
  LOGPRINTF(MMR_REVISION_V);
  LOGPRINTF(".");
  LOGPRINTLN(MMR_BUILD_V);
  
}
