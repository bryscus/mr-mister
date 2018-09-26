/*
  NTP-TZ-DST
  NetWork Time Protocol - Time Zone - Daylight Saving Time

  This example shows how to read and set time,
  and how to use NTP (set NTP0_OR_LOCAL1 to 0 below)
  or an external RTC (set NTP0_OR_LOCAL1 to 1 below)

  TZ and DST below have to be manually set
  according to your local settings.

  This example code is in the public domain.
*/

#include <ESP8266WiFi.h>
#include <time.h>                       // time() ctime()
#include <Time.h>                       // time() ctime()
#include <sys/time.h>                   // struct timeval
#include <coredecls.h>                  // settimeofday_cb()
#include <Dusk2Dawn.h>                  // calculates sunrise/sunset

#define RELAY_ON    1
#define RELAY_OFF   0

//#define PRINT
#define RELAY_CMD

#ifdef PRINT
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
#define LAT             26.899078   // Local latitude for sun calculations
#define LON             -80.164002  // Local longitude for sun calculations
#define MORN_DELAY_S    0           // Time to wait after sunrise
#define NUM_RELAYS      4      

////////////////////////////////////////////////////////
//// TO BE CONVERTED TO RUNTIME ////////////////////////

#define CHAN_1_RUNLENGTH_S  10    //Length of spray
#define CHAN_1_INTERVAL_S   60    //Interval to wait between sprays

////////////////////////////////////////////////////////

#define TZ_MN           ((TZ)*60)
#define TZ_SEC          ((TZ)*3600)
#define DST_SEC         ((DST_MN)*60)

timeval cbtime;      // time set in callback
bool cbtime_set = false;

Dusk2Dawn location(LAT, LON, TZ);

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
}

// for testing purpose:
extern "C" int clock_gettime(clockid_t unused, struct timespec *tp);

#define PTM(w) \
  PRINTF(":" #w "="); \
  PRINTF(tm->tm_##w);

void printTm(const char* what, const tm* tm) {
  PRINTF(what);
  PTM(isdst); PTM(yday); PTM(wday);
  PTM(year);  PTM(mon);  PTM(mday);
  PTM(hour);  PTM(min);  PTM(sec);
}

void printTmShort(const char* what, const tm* tm) {
  PRINTF(what);
  PTM(year+1900);  PTM(mon+1);  PTM(mday);
  PTM(hour);  PTM(min);  PTM(sec);
}

time_t now, midnight;
time_t sunrise_t, sunset_t;
tm *tm;
uint32_t today_s;
int sunrise_s, sunset_s;
int this_day = -1;
int runstart_s, starttime_s;
bool chan_1_msg_flag = true, chan_1_time_flag = true;
bool five_sec_flag = true;
uint32_t five_sec_timer = 0;

void loop() {
  //Read time figures
  now = time(nullptr);
  tm = localtime(&now);

  //Create five second interval timer for display
  if((uint32_t)now > five_sec_timer){
    five_sec_flag = true;
    five_sec_timer = (uint32_t)now + 5;
  }
  
  //Recalculate sunrise/sunset on new day
  if(this_day != tm->tm_yday) {
    uint32_t now_s = (uint32_t)now;
    uint32_t now_hour_s = 3600 * tm->tm_hour;
    uint32_t now_min_s = 60 * tm->tm_min;
    uint32_t now_sec = tm->tm_sec;

    //Calculate midnight to create offset for today
    midnight = (time_t)(now_s - (now_hour_s + now_min_s + now_sec));
    PRINTF("Midnight:");
    PRINTF((uint32_t)midnight);
    printTmShort(" Midnight Time", localtime(&midnight));
    PRINTLN("");

    //Calculate sunrise and sunset
    sunrise_s  = 60 * location.sunrise(tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, true);
    sunset_s   = 60 * location.sunset(tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, true);
    sunrise_t = midnight+(time_t)sunrise_s;
    sunset_t = midnight+(time_t)sunset_s;
    PRINTLN("Recalculated Sunrise/Sunset");
    starttime_s = sunrise_s + MORN_DELAY_S;

    //If new boot, set to current time; else, sunrise + offset
    if(this_day == -1)
      runstart_s = (uint32_t)now - (uint32_t)midnight;
    else
      runstart_s = starttime_s;
    PRINTF("---- Initial runstart_s :");
    PRINTLN(runstart_s);
    this_day = tm->tm_yday;
  }

  //Calculate the number of seconds so far today
  today_s = (uint32_t)now - (uint32_t)midnight;

  //Print human readable time figures every 5 seconds
  if(five_sec_flag){
    tm = localtime(&now);

    PRINTF("Today's:");
    PRINTF(today_s);
    PRINTF(" DoY:");
    PRINTF(tm->tm_yday);
    PRINTF(" DoM:");
    PRINTF(tm->tm_mday);
    PRINTF(" MoY+1:");
    PRINTF(tm->tm_mon+1);
    PRINTF(" Y+1900:");
    PRINTF(tm->tm_year+1900);
    PRINTF(" Time:");
    PRINTF(tm->tm_hour);
    PRINTF(":");
    PRINTF(tm->tm_min);
    PRINTF(":");
    PRINTLN(tm->tm_sec);

    //Print calculated sunrise
    PRINTF("Sunrise:");
    PRINTF(sunrise_s);
    printTmShort("   Sunrise", localtime(&sunrise_t));
    PRINTLN();
    
    //Print calculated sunset
    PRINTF("Sunset :");
    PRINTF(sunset_s);
    printTmShort("    Sunset", localtime(&sunset_t));
    PRINTLN();
  }

  //Guts for running solenoid
  //Should eventually be a class
  if((today_s > sunrise_s) && (today_s < sunset_s))
  {
    if(chan_1_time_flag){
      chan_1_time_flag = false;
      PRINTF("---- Sun is Up! Today's Seconds: ");
      PRINTLN(today_s);
    }

    //Run solenoid during calculated period
    if((today_s >= runstart_s) && (today_s < (runstart_s + CHAN_1_RUNLENGTH_S))){
      if(chan_1_msg_flag){
        chan_1_msg_flag = false;
        PRINTF("---- Running: ");
        PRINTLN(today_s);
      }
      SetRelay(1, RELAY_ON);
    }
    else {
      //Calculate new start time for solenoid
      if(today_s >= (runstart_s + CHAN_1_RUNLENGTH_S)){
        chan_1_msg_flag = true;
        chan_1_time_flag = true;
        runstart_s += CHAN_1_INTERVAL_S;
        PRINTF("---- New runstart_s: ");
        PRINTLN(runstart_s);
      }
      SetRelay(1, RELAY_OFF);
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
