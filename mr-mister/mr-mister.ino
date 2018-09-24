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

#define SSID            "ATT254"
#define SSIDPWD         "$0n@vation!"
#define TZ              -5       // (utc+) TZ in hours
#define DST_MN          60      // use 60mn for summer time in some countries
#define LAT             26.899078
#define LON             -80.164002

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
  delay(3000);
  pinMode(0, OUTPUT);
  pinMode(2, OUTPUT);
  digitalWrite(0, LOW);
  digitalWrite(2, LOW);

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
  PTM(year);  PTM(mon);  PTM(mday);
  PTM(hour);  PTM(min);  PTM(sec);
}

timeval tv;
timespec tp;
time_t now, midnight;
tm *tm;
uint32_t now_ms, now_us, today_s;
int sunrise_s, sunset_s;
int today = -1;

void loop() {

  // simple drifting loop
  delay(5000);

  gettimeofday(&tv, nullptr);
  clock_gettime(0, &tp);
  now = time(nullptr);
  now_ms = millis();
  now_us = micros();

  tm = localtime(&now);

  //Recalculate sunrise/sunset on new day
  if(today != tm->tm_yday) {
    sunrise_s  = 60 * location.sunrise(tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, true);
    sunset_s   = 60 * location.sunset(tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, true);
    today = tm->tm_yday;
    Serial.println("Recalculated Sunrise/Sunset");

    uint32_t now_s = (uint32_t)now;
    uint32_t now_hour_s = 3600 * tm->tm_hour;
    uint32_t now_min_s = 60 * tm->tm_min;
    uint32_t now_sec = tm->tm_sec;
    
    midnight = (time_t)(now_s - (now_hour_s + now_min_s + now_sec));
    Serial.print("Midnight: ");
    Serial.println((uint32_t)midnight);
    printTmShort("midnight", localtime(&midnight));
    Serial.println("");
   }

    today_s = (uint32_t)now - (uint32_t)midnight;
    tm = localtime(&now);

    Serial.print("Day of Year: ");
    Serial.print(tm->tm_yday);
    Serial.print(" Day of Month: ");
    Serial.print(tm->tm_mday);
    Serial.print(" Month of Year: ");
    Serial.print(tm->tm_mon+1);
    Serial.print(" Year: ");
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

  //Print calculated sunset
  Serial.print("  Sunset:");
  Serial.println(sunset_s);
  Serial.println();

    if((today_s > sunrise_s) && (today_s < sunset_s))
    {
       Serial.println("Sun is Up! Today's Seconds: ");
       Serial.println(today_s);
    }
  // localtime / gmtime every second change
//  static time_t lastv = 0;
//  if (lastv != tv.tv_sec) {
//    lastv = tv.tv_sec;
//    printTm("localtime", localtime(&now));
//    Serial.println();
//    printTm("gmtime   ", gmtime(&now));
//    Serial.println();
//    Serial.println();
//
//    digitalWrite(0, HIGH);
//    digitalWrite(2, HIGH);
//  }

  // time from boot
  Serial.print("clock:");
  Serial.print((uint32_t)tp.tv_sec);
  Serial.print("/");
  Serial.print((uint32_t)tp.tv_nsec);
  Serial.print("ns");

  // EPOCH+tz+dst
  Serial.print(" gtod:");
  Serial.print((uint32_t)tv.tv_sec);
  Serial.print("/");
  Serial.print((uint32_t)tv.tv_usec);
  Serial.print("us");

  // EPOCH+tz+dst
  Serial.print(" time:");
  Serial.print((uint32_t)now);

  // human readable
  Serial.print(" ctime:(UTC+");
  Serial.print((uint32_t)(TZ * 60 + DST_MN));
  Serial.print("mn)");
  Serial.print(ctime(&now));
 
}
