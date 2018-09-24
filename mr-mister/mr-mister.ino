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

////////////////////////////////////////////////////////
//// ONE TIME CONFIGURATION PARAMETERS /////////////////

#define SSID            "SSID"
#define SSIDPWD         "Password"
#define TZ              -5          // (utc+) TZ in hours
#define DST_MN          60          // use 60mn for summer time in some countries
#define LAT             26.899078   // Local latitude for sun calculations
#define LON             -80.164002  // Local longitude for sun calculations
#define MORN_DELAY_S    0           // Time to wait after sunrise      

////////////////////////////////////////////////////////
//// TO BE CONVERTED TO RUNTIME ////////////////////////

#define NUM_CHANNELS        1     //Number of channels
#define CHAN_1_RUNLENGTH_S  10    //Length of spray
#define CHAN_1_INTERVAL_S   60    //Interval to wait between sprays
#define CHAN_1_PIN          2     //Pin to drive for channel 1
#define CHAN_1_EN           LOW   //Enable state
#define CHAN_1_DIS          HIGH  //Disable state

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
  Serial.println("------------------ settimeofday() was called ------------------");
}

void setup() {
  
  Serial.begin(115200);
  delay(3000);  //Delay to allow catching AP connection in terminal
  pinMode(CHAN_1_PIN, OUTPUT);
  digitalWrite(CHAN_1_PIN, CHAN_1_DIS);

  settimeofday_cb(time_is_set);

  configTime(TZ_SEC, DST_SEC, "pool.ntp.org");
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, SSIDPWD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(SSID);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println("Setup done");
  // don't wait, observe time changing when ntp timestamp is received
}

// for testing purpose:
extern "C" int clock_gettime(clockid_t unused, struct timespec *tp);

#define PTM(w) \
  Serial.print(":" #w "="); \
  Serial.print(tm->tm_##w);

void printTm(const char* what, const tm* tm) {
  Serial.print(what);
  PTM(isdst); PTM(yday); PTM(wday);
  PTM(year);  PTM(mon);  PTM(mday);
  PTM(hour);  PTM(min);  PTM(sec);
}

void printTmShort(const char* what, const tm* tm) {
  Serial.print(what);
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
    Serial.print("Midnight:");
    Serial.print((uint32_t)midnight);
    printTmShort(" Midnight Time", localtime(&midnight));
    Serial.println("");

    //Calculate sunrise and sunset
    sunrise_s  = 60 * location.sunrise(tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, true);
    sunset_s   = 60 * location.sunset(tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, true);
    sunrise_t = midnight+(time_t)sunrise_s;
    sunset_t = midnight+(time_t)sunset_s;
    Serial.println("Recalculated Sunrise/Sunset");
    starttime_s = sunrise_s + MORN_DELAY_S;

    //If new boot, set to current time; else, sunrise + offset
    if(this_day == -1)
      runstart_s = (uint32_t)now - (uint32_t)midnight;
    else
      runstart_s = starttime_s;
    Serial.print("---- Initial runstart_s :");
    Serial.println(runstart_s);
    this_day = tm->tm_yday;
  }

  //Print human readable time figures every 5 seconds
  if(five_sec_flag){
    tm = localtime(&now);

    Serial.print("DoY:");
    Serial.print(tm->tm_yday);
    Serial.print(" DoM:");
    Serial.print(tm->tm_mday);
    Serial.print(" MoY+1:");
    Serial.print(tm->tm_mon+1);
    Serial.print(" Y+1900:");
    Serial.print(tm->tm_year+1900);
    Serial.print(" Time:");
    Serial.print(tm->tm_hour);
    Serial.print(":");
    Serial.print(tm->tm_min);
    Serial.print(":");
    Serial.println(tm->tm_sec);

    //Print calculated sunrise
    Serial.print("Sunrise:");
    Serial.print(sunrise_s);
    printTmShort("   Sunrise", localtime(&sunrise_t));
    Serial.println();
    
    //Print calculated sunset
    Serial.print("Sunset :");
    Serial.print(sunset_s);
    printTmShort("    Sunset", localtime(&sunset_t));
    Serial.println();
  }

  //Calculate the number of seconds so far today
  today_s = (uint32_t)now - (uint32_t)midnight;

  //Guts for running solenoid
  //Should eventually be a class
  if((today_s > sunrise_s) && (today_s < sunset_s))
  {
    if(chan_1_time_flag){
      chan_1_time_flag = false;
      Serial.print("---- Sun is Up! Today's Seconds: ");
      Serial.println(today_s);
    }

    //Run solenoid during calculated period
    if((today_s >= runstart_s) && (today_s < (runstart_s + CHAN_1_RUNLENGTH_S))){
      if(chan_1_msg_flag){
        chan_1_msg_flag = false;
        Serial.print("---- Running: ");
        Serial.println(today_s);
      }
      digitalWrite(CHAN_1_PIN, CHAN_1_EN);
    }
    else {
      //Calculate new start time for solenoid
      if(today_s >= (runstart_s + CHAN_1_RUNLENGTH_S)){
        chan_1_msg_flag = true;
        chan_1_time_flag = true;
        runstart_s += CHAN_1_INTERVAL_S;
        Serial.print("---- New runstart_s: ");
        Serial.println(runstart_s);
      }
      digitalWrite(CHAN_1_PIN, CHAN_1_DIS);
    }
  }

  //Reset flag
  five_sec_flag = false;
}
