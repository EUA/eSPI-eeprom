#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#include <DNSServer.h>
#include <ArduinoOTA.h>
#include <Ticker.h>
Ticker ticker;

#include <md5.h>
#define _version_ String("v0.1")

#ifdef ESP8266
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
ESP8266WebServer server(80);
#else
#include <WebServer.h>
#include <ESPmDNS.h>
WebServer server(80);
#endif

String UpResult;

String GeneratePage(void) {
  String indexPage = "<!DOCTYPE html><html><head><title>eSPI EEPROM</title></head><body style='background-color:powderblue;'>";
  indexPage += "<h1>eSPI EEPROM "+_version_+"</h1><div>"; \
  indexPage += GetInfoString(true);
  indexPage += "</div>\
  <button type='submit' onclick=\"window.open('download')\">Download EEPROM</button> <br><br>\
  <button type='submit' onclick=\"window.open('readtest')\">Read EEPROM</button> <br> <br>\  
  <button type='submit' onclick=\"window.open('check')\">Check EEPROM</button> <br> <br>\
  <button type='submit' onclick=\"window.open('erase')\">Erase EEPROM</button> <br> <br>\
  <form method='POST' action='/update' enctype='multipart/form-data'><input type='submit' value='Update EEPROM'><input type='file' name='update'> \
  </form> <br>\
  PS:Update function erases ALL EEPROM first, automatically. \
  <br><br><br> Erdem U. Altunyurt, 2019 ";
  
  return indexPage;
}

#include <SerialFlash.h>
#include <SPI.h>
const int FlashChipSelect = SS; //D8 for NodeMCU12E

#define inc_size 2048
unsigned char buff[inc_size];
unsigned char buf[256];

// start reading from the first byte (address 0) of the EEPROM
int address = 0;
int chipsize = 0;

const char* host_name = "eSPI-eeprom";

#define WiFiLed LED_BUILTIN
bool led_on = false;

void tick() {
  digitalWrite(WiFiLed, !digitalRead(WiFiLed));     // set pin to the opposite state
}

//gets called when WiFiManager enters configuration mode
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  ticker.attach(0.1, tick);
}

String GetInfoString( bool isHttp = false );

void setup() {
  pinMode( WiFiLed, OUTPUT );
  digitalWrite( WiFiLed, HIGH );
  for ( int i = 0 ; i < 10 ; i++ ) {
    digitalWrite( WiFiLed, i % 2 );
    delay(100);
  }
  digitalWrite( WiFiLed, HIGH );

  //start UART
  Serial.begin(115200);

  WiFiManager wifiManager;
  //wifiManager.setDebugOutput(false);
  wifiManager.setAPCallback(configModeCallback);
  if (!wifiManager.autoConnect( host_name)) {
    Serial.println("failed to connect and hit timeout");
    ESP.restart();
    delay(1000);
  }
  ticker.detach();
  digitalWrite( WiFiLed, HIGH ); //Shutdown LED

  Serial.println("eSPI EEPROM Program...");

  ArduinoOTA.setHostname(host_name);
  ArduinoOTA.begin();

  //wifi_set_sleep_type(MODEM_SLEEP_T); //This is default one allready
  //This kills OTA
  //wifi_set_sleep_type(LIGHT_SLEEP_T);

  WiFi.hostname( host_name );
  Serial.print(WiFi.localIP());


  server.on("/", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", GeneratePage() );
  });
  server.on("/readtest", eeprom_readtest );
  server.on("/download", eeprom_download );
  server.on("/check", eeprom_check );
  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", UpResult);}, eeprom_update);

  server.on("/erase", eeprom_erase );
  server.begin();

  SerialFlash.begin(FlashChipSelect);
  delay(10);
  Serial.println();
  Serial.println(GetInfoString());
}

void loop() {
  //Wifi connection lost indicator
  if (WiFi.waitForConnectResult() != WL_CONNECTED)
    tick();

  ArduinoOTA.handle();
  server.handleClient();
}

String GetInfoString( bool isHttp ) {
  String info;
  char strfmt[10];
  info = String("Read Chip Identification:");
  info += isHttp ? "<br>" : "\r\n";

  SerialFlash.readID(buf);
  info += "  JEDEC ID:     ";
  for ( int i = 0 ; i <= 2; i++ ) {
    sprintf(strfmt, " %02X", buf[i]);
    info += strfmt;
  }
  info += isHttp ? "<br>" : "\r\n";

  info += "  Part Number: " + String(id2chip(buf));
  info += isHttp ? "<br>" : "\r\n";
  info += "  Memory Size:  " +  String(SerialFlash.capacity(buf)) + " bytes";
  info += isHttp ? "<br>" : "\r\n";
  info += "  Block Size:   " +  String(SerialFlash.blockSize()) + " bytes";
  info += isHttp ? "<br>" : "\r\n";
  info += isHttp ? "<br>" : "\r\n";
  info += isHttp ? "<br>" : "\r\n";
  return info;
}

const char * id2chip(const unsigned char *id) {
  if (id[0] == 0xEF) {
    // Winbond
    if (id[1] == 0x40) {
      if (id[2] == 0x14) return "W25Q80BV";
      if (id[2] == 0x15) return "W25Q16DV";
      if (id[2] == 0x17) return "W25Q64FV";
      if (id[2] == 0x18) return "W25Q128FV";
      if (id[2] == 0x19) return "W25Q256FV";
    }
  }
  if (id[0] == 0x01) {
    // Spansion
    if (id[1] == 0x02) {
      if (id[2] == 0x16) return "S25FL064A";
      if (id[2] == 0x19) return "S25FL256S";
      if (id[2] == 0x20) return "S25FL512S";
    }
    if (id[1] == 0x20) {
      if (id[2] == 0x18) return "S25FL127S";
    }
  }
  if (id[0] == 0xC2) {
    // Macronix
    if (id[1] == 0x20) {
      if (id[2] == 0x18) return "MX25L12805D";
    }
  }
  if (id[0] == 0x20) {
    // Micron
    if (id[1] == 0xBA) {
      if (id[2] == 0x20) return "N25Q512A";
      if (id[2] == 0x21) return "N25Q00AA";
    }
    if (id[1] == 0xBB) {
      if (id[2] == 0x22) return "MT25QL02GC";
    }
  }
  if (id[0] == 0xBF) {
    // SST
    if (id[1] == 0x25) {
      if (id[2] == 0x02) return "SST25WF010";
      if (id[2] == 0x03) return "SST25WF020";
      if (id[2] == 0x04) return "SST25WF040";
      if (id[2] == 0x41) return "SST25VF016B";
      if (id[2] == 0x4A) return "SST25VF032";
    }
    if (id[1] == 0x25) {
      if (id[2] == 0x01) return "SST26VF016";
      if (id[2] == 0x02) return "SST26VF032";
      if (id[2] == 0x43) return "SST26VF064";
    }
  }
  return "(unknown chip)";
}

void eeprom_check( void ){
  server.sendHeader("Connection", "close");
  server.send ( 200, "text/plain", check_eeprom_empty() ? "Flash Memory is empty." : "Flash Memory is NOT empty.");
}
bool check_eeprom_empty(){
  SerialFlash.readID(buf);
  chipsize = SerialFlash.capacity(buf);
  Serial.println("Checking Flash Memory.");
  byte test=0xFF;
  address=0;
  uint rd_size;
  while (address < chipsize) {
    rd_size = chipsize - address > inc_size ? inc_size : chipsize - address;
    SerialFlash.read(address, (uint32_t*)buff, rd_size);
    address += rd_size;
    for(int i=0;i<rd_size;i++)
      test&=buff[i];
  }
  if( test == 0xFF ){
    Serial.println("Flash Memory is empty.");
    return true;
    }
  Serial.println("Flash Memory is NOT empty.");
  return false;
  }

void eeprom_erase( void ) {
  eeprom_eraser( false );
}
void eeprom_eraser( bool internal ) {
  //server.send(200, "text/plane", "Erasing EEPROM...");
  Serial.setDebugOutput(true);
  if( !  check_eeprom_empty() ){
    SerialFlash.eraseAll();
    Serial.println("Erasing ALL Flash Memory. Please Wait.");
    if(!internal){
      server.sendHeader("Connection", "close");
      server.send ( 200, "text/plain", "NOT empty. Erasing EEPROM...");
      }
    while (SerialFlash.ready() == false) {
      yield();
      tick();
      delay(100);
      }
    }
  if(!internal){
      server.sendHeader("Connection", "close");
      server.send ( 200, "text/plain", "Allready empty. Skip erasing EEPROM...");
      }
  Serial.setDebugOutput(false);
  digitalWrite( WiFiLed, HIGH );
};

void eeprom_download(void) {
  SerialFlash.readID(buf);
  chipsize = SerialFlash.capacity(buf);
  server.sendHeader("Connection", "close");
  server.setContentLength(chipsize);
  server.send ( 200, "application/octet-stream", "");
  Serial.printf("Reading EEPROM\r\n");
  address = 0;
  unsigned long startMillis = millis();
  unsigned long endMillis = millis();
  unsigned long lastMillis = millis();

  Serial.printf("Sending Address: %u\r\n", address);
  while (address < chipsize) {
    SerialFlash.read(address, (uint32_t*)buff, inc_size);
    address += inc_size;
    //server.sendContent(buff);
    server.client().write(buff, inc_size);
    tick();
    yield();
    lastMillis = millis();
    if (lastMillis - endMillis > 1000 ) {
      uint32_t spd = ( (address / 1024) / ((endMillis - startMillis) / 1000.0));
      Serial.printf("%% %0.2f, Sending Address: %u Throuput %u Kbps\r\n", address * 100.0 / chipsize, address, spd  );
      endMillis = lastMillis;
    }
  }
  uint32_t spd = ( (address / 1024) / ((endMillis - startMillis) / 1000.0));
  Serial.printf("%%100.00, Avarage throuput %u Kbps\r\n", spd);
  digitalWrite( WiFiLed, HIGH );
}

void eeprom_readtest(void){
  SerialFlash.readID(buf);
  chipsize = SerialFlash.capacity(buf);
  server.sendHeader("Connection", "close");
  server.send ( 200, "text/plain", eeprom_md5(chipsize));
  }
  
String eeprom_md5( uint32_t check_size ) {
  MD5Builder md5;
  md5.begin();
  Serial.printf("Reading EEPROM up to %u bytes\r\n", check_size);
  address = 0;
  unsigned long startMillis = millis();
  unsigned long endMillis = millis();
  unsigned long lastMillis = millis();
  int rd_size;
  while (address < check_size) {
    rd_size = check_size - address > inc_size ? inc_size : check_size - address;
    SerialFlash.read(address, (uint32_t*)buff, rd_size);
    md5.add(buff,rd_size);
    address += rd_size;
    tick();
    yield();
    lastMillis = millis();
    if (lastMillis - endMillis > 1000 ) {
      uint32_t spd = ( (address / 1024) / ((endMillis - startMillis) / 1000.0));
      Serial.printf("%% %0.2f, Address: %u Throuput %u Kbps\r\n", address * 100.0 / chipsize, address, spd  );
      endMillis = lastMillis;
      }
    }
  uint32_t spd = ( (address / 1024) / ((lastMillis - startMillis) / 1000.0));
  Serial.printf("%%100.00, Avarage throuput %u Kbps\r\n", spd);
  digitalWrite( WiFiLed, HIGH );
  md5.calculate();
  
  String result="EEPROM MD5:";
  result+=md5.toString();
  Serial.println(result); // can be getChars() to getBytes() too. (16 chars) .. see above 
  return md5.toString();
}

void eeprom_update( void ) {
  static MD5Builder md5;
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    SerialFlash.readID(buf);
    chipsize = SerialFlash.capacity(buf);
    //WiFiUDP::stopAll();
    md5.begin();
    eeprom_eraser(true);
    Serial.printf("Uploading: %s\n", upload.filename.c_str());
    address=0;
    UpResult="";
    }
  else if (upload.status == UPLOAD_FILE_WRITE) {
      if(upload.totalSize > chipsize){
        if(UpResult==""){
          UpResult+=("File bigger than chip size. Will be truncated by chipsize (%u).  ",chipsize);
          Serial.printf(UpResult.c_str());
          }
        Serial.printf("Skipping Address: %u \r\n", address);
        }
      else{
        Serial.printf("Writing Address: %u \r\n", address);
        if(upload.currentSize==0){
          UpResult="No or Empty File Selected! ";
          Serial.printf(UpResult.c_str());
          }
        else
          SerialFlash.write(address, upload.buf, upload.currentSize);
        address += upload.currentSize;
        md5.add(upload.buf, upload.currentSize);
        tick();
        }
  }else if (upload.status == UPLOAD_FILE_END) {
      Serial.println("Write Done.");
      md5.calculate();
      String writen=md5.toString();
      Serial.println("Writen MD5:"+writen);
      String readed=eeprom_md5(address);
      if( writen == readed ){
        UpResult+="Write Successful";
        Serial.printf(UpResult.c_str());
        }
      else{
        UpResult+="Write Failed";
        Serial.printf(UpResult.c_str());
        }
      digitalWrite( WiFiLed, HIGH );
    }
    yield();
  }
