// Using an ESP8266 on a Wemos D1 mini board: https://wiki.wemos.cc/products:d1:d1_mini

#include <SPI.h> 
#include "SdFat.h"
#include "sdios.h"
#include <WEMOS_SHT3X.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

// for sprintf()
char charbuff[100];

SHT3X tempSensor(0x45);

/* SD CARD*/
using namespace sdfat;

// Create a Serial output stream.
ArduinoOutStream cout(Serial);


// SD card chip select pin.
// D8 corresponds to GPIO15 which is a special Pin used for Boot: https://github.com/esp8266/esp8266-wiki/wiki/Boot-Process
// It needs to be LOW in order for the ESP8266 to boot "normally" from its SPI Flash, and it does have a 10K pull down resistor on the board
// HOWEVER, with my SD Card shield, when the SD card is inserted it takes this pin UP
// I had to solder an additional 1K resistor between D8 and GND, which seems to fix the problem
const uint8_t chipSelect = D8;

// File system object.
SdFat sd;
SdFile file;


/* TIME KEEPING */
const char* ntpServer = "ch.pool.ntp.org";
const char* ssid      = "***";
const char* password  = "***";

// Seconds since 1st Jan 1900 + 3782926500  (just so that we don't waste all this space for nothing)
unsigned long SECS_OFFSET = 3782926500;
unsigned long _secondsWhenMCUStarted = 0;
unsigned long DEEP_SLEEP_MILLIS = 60 * 1000; // 1 minute


void setup() {
  Serial.begin(115200);
    
  pinMode(LED_BUILTIN, OUTPUT);     // Initialize the LED_BUILTIN pin as an output
  digitalWrite(LED_BUILTIN, LOW);

  
  cout << "\nSecs WHEN MCU Started: " << getULongFromRTC(64) << " / Millis SINCE MCU Started: " << (getULongFromRTC(65) + millis()) << "\n";

  /* 1. use WiFi and an NTP server to try and adjust our TIME */
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  int n = WiFi.scanNetworks();
  bool foundKnownSSID = false;
  for (int i = 0; i < n; i++) {
    cout << WiFi.SSID(i).c_str() << "\n";
    
    if(WiFi.SSID(i) == ssid) foundKnownSSID = true;
  }

  // only if we found a specific SSID
  if(foundKnownSSID) {
    WiFi.begin (ssid, password);
    // wait for a connection max 10secs
    for(int i=0; WiFi.status() != WL_CONNECTED && i<100; i++) {
      cout << "."; 
      delay(100);
    }

    if(WiFi.status() != WL_CONNECTED){
      cout << "Can't connect to WiFi in 10secs. Ignore." << "\n";
    } else {
      cout << "WiFi connected" << "\n";
  
      sendNTPrequest();
      unsigned long secondsSince1900 = parseNTPresponse();
      // wait for a reponse, but max 3 seconds
      for(int i=0; secondsSince1900 == 0 && i < 3000; i++) {
        delay(1);
        
        secondsSince1900 = parseNTPresponse();
      }
  
      if(secondsSince1900 == 0) {
        cout << "\nCan't get NTP time from " << ntpServer << " in less than 3 seconds. Ignore\n";
      } else {
        _secondsWhenMCUStarted = secondsSince1900 - SECS_OFFSET - millis() / 1000;
        saveULongInRTC(64, _secondsWhenMCUStarted); // so that we remember this during deep-sleep
        saveULongInRTC(65, 0UL); // these are the millis since _secondsWhenMCUStarted
      
        cout << "REFRESHED Time from NTP/ Seconds since 1900: " << secondsSince1900 << "\n";
      }
    }
  }

  
  _secondsWhenMCUStarted = getULongFromRTC(64);
  cout << "Using RTC saved, Seconds since MCU started: " << _secondsWhenMCUStarted << "\n";

  
  /* 2. SD Card setup and file opening*/

  // Initialize at the highest speed supported by the board that is not over 50 MHz. 
  // Try a lower speed if SPI errors occur.
  if (!sd.begin(chipSelect, SD_SCK_MHZ(10))) {
    cout << "\nCan't start DS Fat system\n";
  } else {

    //  cout << "\nList of files on the SD.\n";
    //  sd.ls("/", LS_R);
  
    if(! file.open("/temperatures_log.csv", O_WRITE | O_CREAT | O_APPEND)){
      cout << "Can't open file\n";
    } else {
  
      cout << "File size:  " <<  file.fileSize() << "\n";
      
    
      /* 3. TEMP & Humidity */
      if(tempSensor.get() == 0){
        cout << "\nSecs,Temp,Humidity\n";
    
        sprintf(charbuff, "%d,%.2f,%.2f\n", getSecs(), tempSensor.cTemp, tempSensor.humidity);
        cout << charbuff;
        file.write(charbuff);
      } else{
        cout << "Error!\n";
        
        file.write("___ERROR\n");
      }
      
      file.sync();
    }
  }

  /* 4. SLEEP */
  // not before saving the microseconds offset when we'll wake up (as the RTC will be reset during deep sleep (but the RTC memory is still available))
  // Offset when we wake up = Offset when we woke up + millis since then + millis of sleep
  // using Millisecs rather than Micros, so that the Unsigned Long storing this resets only after ~50days, rather than just a bit more than 1 hour
  // this means we can keep time WITHOUT Internet/NTP for up to 50days.
  // Not using Seconds to minimize drift (losing / adding up to half a second every time we go to sleep might mean up to 10mins per day if we slepp for only 1 minute at a time)
  unsigned long millisSinceMCUStartedWhenWeWakeUp = millisSinceStartIncludingDeepSleep() + DEEP_SLEEP_MILLIS;
  saveULongInRTC(65, millisSinceMCUStartedWhenWeWakeUp);  
  
  // for the ESP8266 to come back to life afterwards GPIO16 needs to be connected to RST
  // on the D1 mini Wemos board the former is D0. There's also a nice "SLEEP" jumper on the back! 
  // HOWEVER when this connection is done the ESP8266 can't be programmed anymore
  // So I added a SWITCH which is "off" when programming and then "on" when I need the sleep to work
  // In my case the board uses ~125mA in normal mode -> ~165uA when in deep sleep
  ESP.deepSleep(DEEP_SLEEP_MILLIS * 1000);  // this is in Micros
}




unsigned long getSecs() {
  return _secondsWhenMCUStarted +  millisSinceStartIncludingDeepSleep() / 1000;
}

//  system_get_time() is reset when waking from deep sleep, so we add what we stored just before deep-sleep
unsigned long millisSinceStartIncludingDeepSleep() {
  return getULongFromRTC(65) + millis();
}



/* NTP */

const int NTP_PACKET_SIZE = 48;
byte _ntpBuff[ NTP_PACKET_SIZE];
WiFiUDP udpNtpClient;

void sendNTPrequest() {
  udpNtpClient.begin(1337); // Port for NTP receive

  memset(_ntpBuff, 0, NTP_PACKET_SIZE);
  _ntpBuff[0] = 0b11100011; // LI, Version, Mode
  _ntpBuff[1] = 0;          // Stratum, or type of clock
  _ntpBuff[2] = 6;          // Polling Interval
  _ntpBuff[3] = 0xEC;       // Peer Clock Precision
  _ntpBuff[12] = 49;
  _ntpBuff[13] = 0x4E;
  _ntpBuff[14] = 49;
  _ntpBuff[15] = 52;
  udpNtpClient.beginPacket(ntpServer, 123);
  udpNtpClient.write(_ntpBuff, NTP_PACKET_SIZE);
  udpNtpClient.endPacket();
}

// returns Excel time, ie. seconds since 1st Jan 1900
// in Excel do "=secsSince1900/86400 + 2" in a cell formatted as time to get the human readable form (86400 secs in a day + 2 days not sure why)
unsigned long parseNTPresponse() {
  int cb = udpNtpClient.parsePacket();
  if(cb == 0) {
    return 0; // haven't received a response yet
  } else {
    udpNtpClient.read(_ntpBuff, NTP_PACKET_SIZE); // read the packet into the buffer
    unsigned long highWord = word(_ntpBuff[40], _ntpBuff[41]);
    unsigned long lowWord = word(_ntpBuff[42], _ntpBuff[43]);
    unsigned long secsSince1900 = highWord << 16 | lowWord;
  
    return secsSince1900;    
  }
}




/* RTC Memory to keep system time during deep-sleep*/

/**
 * Data read/write accesses to the RTC memory must be word-aligned (4-byte boundary aligned). 
 * Parameter des_addr means block number (4 bytes per block). 
 * For example, to save data at the beginning of user data area, des_addr will be 256/4 = 64, and save_sizewill be data length.
 */
bool saveULongInRTC(int block, unsigned long data) {
  return system_rtc_mem_write(
    block,
    &data,
    sizeof(data)
    );
}

unsigned long getULongFromRTC(int block) {
  unsigned long data;
  system_rtc_mem_read(
    block,
    &data,
    sizeof(data)
    );

  return data;
}





void loop() {
  // NOTHING given that it goes to SLEEP at the end of setup() and then when it resets it starts from scratch
}
