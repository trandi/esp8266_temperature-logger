// Using an ESP8266 on a Wemos D1 mini board: https://wiki.wemos.cc/products:d1:d1_mini

#include <SPI.h> 
#include "SdFat.h"
#include "sdios.h"
#include <WEMOS_SHT3X.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include<ESP8266WiFi.h>

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
// https://github.com/greiman/SdFat
SdFat sd;
SdFile file;
const char* csvFile = "/temperatures_log.csv";
const byte LINE_BUFF_SIZE = 50;
char line_buffer[LINE_BUFF_SIZE];

/* WiFi */
const char* ssid      = "***";
const char* password  = "***";

/* Upload data to the cloud */
String thingspeakServer = "api.thingspeak.com";
String thingspeakChannel = "***";
String thingspeakWriteKey = "***";

/* TIME KEEPING */
const char* ntpServer = "ch.pool.ntp.org";

// Seconds since 1st Jan 1900 + 3782926500  (just so that we don't waste all this space for nothing)
unsigned long SECS_OFFSET = 3782926500;
unsigned long _secondsWhenMCUStarted = 0;
unsigned long DEEP_SLEEP_MILLIS = 5 * 60 * 1000;
double TIME_DRIFT_FRACTION = 0.983; //empirically found that real time is SLOWER than what this MCU thinks


/* ESP8266 RTC system memory that persists during deep sleep */
const int RTC_MEM_ADDR_SECONDS_MCU_STARTED = 64; // _secondsWhenMCUStarted so that we remember this during deep-sleep
const int RTC_MEM_ADDR_MILLIS_SINCE_MCU_STARTED = 65; //millis since _secondsWhenMCUStarted
const int RTC_MEM_ADDR_SECONDS_LASTLINE_UPLOADED_CLOUD = 66; //seconds written to CSV file of LAST line sent to the cloud (so that we know where to start from the next time)



void setup() {
  Serial.begin(115200);
    
  pinMode(LED_BUILTIN, OUTPUT);     // Initialize the LED_BUILTIN pin as an output
  digitalWrite(LED_BUILTIN, LOW);

  
  cout << endl << "Secs WHEN MCU Started: " << getULongFromRTC(RTC_MEM_ADDR_SECONDS_MCU_STARTED) << " / Millis SINCE MCU Started: " << (getULongFromRTC(RTC_MEM_ADDR_MILLIS_SINCE_MCU_STARTED) + millis()) << endl;

  /* 1. use WiFi and an NTP server to try and adjust our TIME */
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  int n = WiFi.scanNetworks();
  bool foundKnownSSID = false;
  for (int i = 0; i < n; i++) {
    cout << WiFi.SSID(i).c_str() << endl;
    
    if(WiFi.SSID(i) == ssid) foundKnownSSID = true;
  }

  // only if we found a specific SSID
  if(foundKnownSSID) {
    WiFi.begin (ssid, password);
    // wait for a connection max 10secs
    for(int i=0; WiFi.status() != WL_CONNECTED && i<100; i++) {
      cout << "."; 
      delay(200);
    }

    if(WiFi.status() != WL_CONNECTED){
      cout << endl << "Can't connect to WiFi in 20secs. Ignore." << endl;
    } else {
      cout << "WiFi connected." << endl;

      /* NTP */
      sendNTPrequest();
      unsigned long secondsSince1900 = parseNTPresponse();
      // wait for a reponse, but max 3 seconds
      for(int i=0; secondsSince1900 == 0 && i < 3000; i++) {
        delay(1);
        
        secondsSince1900 = parseNTPresponse();
      }
  
      if(secondsSince1900 == 0) {
        cout << endl << "Can't get NTP time from " << ntpServer << " in less than 3 seconds. Ignore." << endl;
      } else {
        _secondsWhenMCUStarted = secondsSince1900 - SECS_OFFSET - millis() / 1000;
        saveULongInRTC(RTC_MEM_ADDR_SECONDS_MCU_STARTED, _secondsWhenMCUStarted); // so that we remember this during deep-sleep
        saveULongInRTC(RTC_MEM_ADDR_MILLIS_SINCE_MCU_STARTED, 0UL); // these are the millis since _secondsWhenMCUStarted
      
        cout << "REFRESHED Time from NTP/ Seconds since 1900: " << secondsSince1900 << endl;
      }      
    }
  }

  
  _secondsWhenMCUStarted = getULongFromRTC(RTC_MEM_ADDR_SECONDS_MCU_STARTED);
  cout << "Using RTC saved, Seconds since MCU started: " << _secondsWhenMCUStarted << endl;

  
  /* 2. SD Card setup and file opening*/

  // Initialize at the highest speed supported by the board that is not over 50 MHz. 
  // Try a lower speed if SPI errors occur.
  if (!sd.begin(chipSelect, SD_SCK_MHZ(10))) {
    cout << endl << "Can't start DS Fat system" << endl;
    drawAttentionLEDsignals(200);
  } else {

    //  cout << endl << "List of files on the SD." << endl;
    //  sd.ls("/", LS_R);
  
    if(! file.open(csvFile, O_WRITE | O_CREAT | O_APPEND)){
      cout << "Can't open file: " << csvFile << " for writing." << endl;
    } else {
  
      cout << "File size:  " <<  file.fileSize() << endl;
      
      /* 3. TEMP & Humidity */
      if(tempSensor.get() == 0){
        cout << endl << "Secs,Temp,Humidity" << endl;
    
        sprintf(charbuff, "%d,%.2f,%.2f\n", getSecs(), tempSensor.cTemp, tempSensor.humidity);
        cout << charbuff;
        file.write(charbuff);
      } else{
        cout << "Error!" << endl;
        
        file.write("___ERROR\n");
      }
      
      file.sync();
      file.close();
    }

    
    /* 4. Try and Update data to ThingSpeak */
    if(WiFi.status() == WL_CONNECTED){
      cout << "Have an SD file and a WiFi connection, update data to the cloud." << endl;
      
      ifstream sdin(csvFile);
      String updates(""); // that's what we want to send, in the right format 
      int line_number = 0;
      unsigned long secsOfLastLineAlreadySent = getULongFromRTC(RTC_MEM_ADDR_SECONDS_LASTLINE_UPLOADED_CLOUD);
      String secsOfLastLineAlreadySentStr = String(secsOfLastLineAlreadySent);
      long previousLineSeconds = 0;
      boolean addToUpdates = false;

      while(sdin.getline(line_buffer, LINE_BUFF_SIZE, '\n')) {
        String line(line_buffer);
        long lineSeconds = parseFirstField(line);
        long relativeSeconds = lineSeconds - previousLineSeconds;
        if(relativeSeconds < 0) relativeSeconds = 0;
        previousLineSeconds = lineSeconds;

        if(addToUpdates) {
          updates = updates + relativeSeconds + "," + removeFirstField(line) + "|";
        }

        if(line.indexOf(secsOfLastLineAlreadySentStr) >= 0){
          // this was the last line already sent/uploaded, from now on add to the updates
          addToUpdates = true;
        }
      }

      // send the data to the cloud
      int httpRequestResponse = thingspeakHttpRequest(updates);
      if(httpRequestResponse == 202) {
        // now remember the last line that was sent
        saveULongInRTC(RTC_MEM_ADDR_SECONDS_LASTLINE_UPLOADED_CLOUD, previousLineSeconds);

        cout << "SUCCESSFULLY sent updates to the CLOUD (last line " << previousLineSeconds << "): ";
      } else {        
        cout << "ERROR sending updates to the CLOUD (" << httpRequestResponse << "): ";
        drawAttentionLEDsignals(100);
      }
      cout << updates.c_str() << endl;
    }
  }



  /* 4. SLEEP */
  cout << "Now going to a well deserved SLEEP!" << endl;
  // not before saving the microseconds offset when we'll wake up (as the RTC will be reset during deep sleep (but the RTC memory is still available))
  // Offset when we wake up = Offset when we woke up + millis since then + millis of sleep
  // using Millisecs rather than Micros, so that the Unsigned Long storing this resets only after ~50days, rather than just a bit more than 1 hour
  // this means we can keep time WITHOUT Internet/NTP for up to 50days.
  // Not using Seconds to minimize drift (losing / adding up to half a second every time we go to sleep might mean up to 10mins per day if we slepp for only 1 minute at a time)
  unsigned long millisSinceMCUStartedWhenWeWakeUp = millisSinceStartIncludingDeepSleep() + DEEP_SLEEP_MILLIS;
  saveULongInRTC(RTC_MEM_ADDR_MILLIS_SINCE_MCU_STARTED, millisSinceMCUStartedWhenWeWakeUp);  
  
  // for the ESP8266 to come back to life afterwards GPIO16 needs to be connected to RST
  // on the D1 mini Wemos board the former is D0. There's also a nice "SLEEP" jumper on the back! 
  // HOWEVER when this connection is done the ESP8266 can't be programmed anymore
  // So I added a SWITCH which is "off" when programming and then "on" when I need the sleep to work
  // In my case the board uses ~125mA in normal mode -> ~165uA when in deep sleep
  ESP.deepSleep(DEEP_SLEEP_MILLIS * 1000);  // this is in Micros
}


long parseFirstField(String line) {
  return line.substring(0, line.indexOf(',')).toInt();
}

String removeFirstField(String line) {
  return line.substring(line.indexOf(',') + 1);
}

unsigned long getSecs() {
  unsigned long secondsSinceMCUStarted = _secondsWhenMCUStarted +  millisSinceStartIncludingDeepSleep() / 1000 * TIME_DRIFT_FRACTION;
  return secondsSinceMCUStarted;
}

//  system_get_time() is reset when waking from deep sleep, so we add what we stored just before deep-sleep
unsigned long millisSinceStartIncludingDeepSleep() {
  return getULongFromRTC(RTC_MEM_ADDR_MILLIS_SINCE_MCU_STARTED) + millis();
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



/*** Upload data to ThingSpeak.com ***/
WiFiClient client;

/**
 * Updates the ThingSpeakchannel with data
 * Format expected https://uk.mathworks.com/help/thingspeak/bulkwritecsvdata.html
 * 
 * The number of messages in a single bulk-update is limited to 960 messages for users of free accounts. 
 * The time interval between sequential bulk-update calls must be 15 seconds or more.
 */
int thingspeakHttpRequest(String updates) {
  if(updates.length() == 0) {
    cout << "Nothing to send to the CLOUD" << endl;
    return 202; // all good
  } else {
    // Format the data buffer as noted above
    String data = "write_api_key=" + thingspeakWriteKey + "&time_format=relative&updates=" + updates;
    
    client.stop(); // Close any connection before sending a new request
  
    // POST data to ThingSpeak
    if (client.connect(thingspeakServer, 80)) {
      client.println("POST /channels/" + thingspeakChannel + "/bulk_update.csv HTTP/1.1");
      client.println("Host: " + thingspeakServer);
      //client.println("User-Agent: blabla");
      client.println("Connection: close");
      client.println("Content-Type: application/x-www-form-urlencoded");
      client.println("Content-Length: " + String(data.length()));
      client.println();
      client.println(data);
    } else {
      cout << "Failure: Failed to connect to: " << thingspeakServer << endl;
    }
    delay(250); //Wait to receive the response
    client.parseFloat();
    
    return client.parseInt(); // 202 indicates that the server has accepted the response
  }
}



void drawAttentionLEDsignals(int intervalMillis) {
  // try to draw attention !
  for(int i=0; i<10; i++){
    digitalWrite(LED_BUILTIN, HIGH);
    delay(intervalMillis);
    digitalWrite(LED_BUILTIN, LOW);
    delay(intervalMillis);
  }  
}


void loop() {
  // NOTHING given that it goes to SLEEP at the end of setup() and then when it resets it starts from scratch
}
