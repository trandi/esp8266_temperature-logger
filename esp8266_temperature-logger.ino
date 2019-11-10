#include <SPI.h> 
#include "SdFat.h"
#include "sdios.h"
#include <WEMOS_SHT3X.h>


SHT3X tempSensor(0x45);

using namespace sdfat;

// Create a Serial output stream.
ArduinoOutStream cout(Serial);

// Buffer for Serial input.
char cinBuf[40];

// Create a serial input stream.
ArduinoInStream cin(Serial, cinBuf, sizeof(cinBuf));



// SD card chip select pin.
const uint8_t chipSelect = D8;
//------------------------------------------------------------------------------

// File system object.
SdFat sd;
SdFile file;


void setup() {
  Serial.begin(115200);
  
  pinMode(LED_BUILTIN, OUTPUT);     // Initialize the LED_BUILTIN pin as an output

  cout << F("Type any character to start\n");
  // Wait for input line and discard.
  cin.readline();
  cout << endl;

//  // Initialize at the highest speed supported by the board that is
//  // not over 50 MHz. Try a lower speed if SPI errors occur.
//  if (!sd.begin(chipSelect, SD_SCK_MHZ(10))) {
//    sd.initErrorPrint();
//  }
//
//  cout << "\nList of files on the SD.\n";
//  sd.ls("/", LS_R);
//
//  if(! file.open("/esp8266_data", O_WRITE | O_CREAT | O_APPEND)){
//    cout << "Can't open file\n";
//  }
//
//  cout << "File size:  " <<  file.fileSize() << "\n";
//  
//
//  file.write("StartOfFile\n");
//  file.sync();
  
}

// the loop function runs over and over again forever
void loop() {
  digitalWrite(LED_BUILTIN, LOW);   // Turn the LED on (Note that LOW is the voltage level
  // but actually the LED is on; this is because
  // it is active low on the ESP-01)
  delay(500);                      // Wait for a second
  digitalWrite(LED_BUILTIN, HIGH);  // Turn the LED off by making the voltage HIGH
  delay(500);                      // Wait for two seconds (to demonstrate the active low LED)

//  file.write("toto\n");
//  file.sync();


  // TEMP & Humidity
  if(tempSensor.get()==0){
    cout << "Temp: " << tempSensor.cTemp << "°C / Humidity: " << tempSensor.humidity << "%\n";
  } else{
    cout << "Error!\n";
  }
}
