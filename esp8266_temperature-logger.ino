// Using an ESP8266 on a Wemos D1 mini board: https://wiki.wemos.cc/products:d1:d1_mini

#include <SPI.h> 
#include "SdFat.h"
#include "sdios.h"
#include <WEMOS_SHT3X.h>

SHT3X tempSensor(0x45);

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

// for sprintf()
char charbuff[100];

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);     // Initialize the LED_BUILTIN pin as an output
  digitalWrite(LED_BUILTIN, LOW); 
  
  Serial.begin(115200);
  delay(500); //give time to the Serial connection to initialise
  cout << "\n\nStarted\n\n";


  // Initialize at the highest speed supported by the board that is not over 50 MHz. 
  // Try a lower speed if SPI errors occur.
  if (!sd.begin(chipSelect, SD_SCK_MHZ(10))) {
    cout << "Can't start DS Fat system\n";
  }

//  cout << "\nList of files on the SD.\n";
//  sd.ls("/", LS_R);

  if(! file.open("/temperatures_log.csv", O_WRITE | O_CREAT | O_APPEND)){
    cout << "Can't open file\n";
  }

  cout << "File size:  " <<  file.fileSize() << "\n";
  

  // TEMP & Humidity
  if(tempSensor.get()==0){
    cout << "Temp: " << tempSensor.cTemp << "°C / Humidity: " << tempSensor.humidity << "%\n";

    sprintf(charbuff, "%.2f,%.2f\n", tempSensor.cTemp, tempSensor.humidity);
    file.write(charbuff);
  } else{
    cout << "Error!\n";
    
    file.write("___ERROR\n");
  }
  
  file.sync();
  

  // for the ESP8266 to come back to life afterwards GPIO16 needs to be connected to RST
  // on the D1 mini Wemos board the former is D0. There's also a nice "SLEEP" jumper on the back! 
  // HOWEVER when this connection is done the ESP8266 can't be programmed anymore
  // So I added a SWITCH which is "off" when programming and then "on" when I need the sleep to work
  // In my case the board uses ~125mA in normal mode -> ~165uA when in deep sleep
  ESP.deepSleep(60e6); // 1 minute
}

void loop() {
  // NOTHING given that it goes to SLEEP at the end of setup() and then when it resets it starts from scratch
}
