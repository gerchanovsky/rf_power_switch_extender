/*
  Example for receiving
  Wall switch RF extender. Plug MCU box to switching power outlet to control remote 433MHz RF relay

  Used Fairchild MCT6 Optocoupler
  
  $33Mhz RC control library
  https://github.com/sui77/rc-switch/
  Emulated Belkin WeMo devices that work with the Amazon Echo 
  https://github.com/makermusings/fauxmo  
  https://bitbucket.org/xoseperez/fauxmoESP

  https://github.com/me-no-dev/ESPAsyncTCP
  https://github.com/me-no-dev/ESPAsyncWebServer
*/
//#define ENABLE_WEB
#define ENABLE_WEMO

#include <ESP8266WiFi.h>
//#include <WiFiClient.h>
//#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <RCSwitch.h>
#include <ESPAsyncUDP.h>
#include <ESPAsyncTCP.h>
#ifdef ENABLE_WEMO
#include <fauxmoESP.h>            // Belkin WeMo/Amazon Alexa stuff
fauxmoESP fauxmo;
#endif

#if 1
#include "/Users/alex/credentials.h"
#else
#define WIFI_SSID "wifiAP"
#define WIFI_PASS "wifiPASSWORD"
#endif

#define RETRY_CONNECT 60
#define HOST_NAME "Alice-Room"
#define HOST_MDNS "alice-room"
#define HOST_DESC "Alice's bedroom"
#define WEMO_DEV "bedroom"

RCSwitch mySwitch = RCSwitch();

static const unsigned int etek_PulseLength = 192;
static const unsigned int etek_protocol = 1;
static const unsigned int etek_lengthBit = 24;
static const unsigned long etek_4_on  = 0x445D03;
static const unsigned long etek_4_off = 0x445D0C;
static const unsigned long etek_5_on  = 0x447503;
static const unsigned long etek_5_off = 0x44750C;

#define ac_inputPin   D4 //GPIO2 is D4 on WeMos D1 mini
#define led_outputPin D1 //GPIO5 is D1 on WeMos D1 mini
bool power_on = false;
int power_delayed = -1;

void power(bool on, const __FlashStringHelper *str = NULL, bool send_rf = true)
{
  power_on = on;
  digitalWrite(led_outputPin, !on);
  if (send_rf) {
    mySwitch.send(on?etek_5_on:etek_5_off, etek_lengthBit);
  }
  if (str)
    Serial.println(str);    
}

bool AC_on = false;
long AC_millis = 0;
int  AC_periods = 0;
#define AC_periods_min 10
#define AC_timeout (1000*AC_periods_min/50)
uint32_t _millis = 0;

#ifdef ENABLE_WEB
AsyncWebServer server(80);
void setup_server()
{

static String webPage;
  webPage = "<h1>Welcome to "+HOST_DESC+" light control</h1>";
  webPage += "<p>Main light <a href=\"on\"><button background=\"olive\">ON</button></a>&nbsp;<a href=\"off\"><button>OFF</button></a></p>";
  
  MDNS.addService("http","tcp",80);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", webPage);
  });
  server.on("/on", HTTP_GET, [](AsyncWebServerRequest *request){
    power_delayed = 1;
    request->send(200, "text/html", webPage);//request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });
  server.on("/off", HTTP_GET, [](AsyncWebServerRequest *request){
    power_delayed = 0;
    request->send(200, "text/html", webPage);
  });
  server.begin();
}
#else
#define setup_server()
#endif
void setup() {
  WiFi.hostname(HOST_NAME);

  Serial.begin(115200);
  mySwitch.enableReceive(D3);  //D3->GPIO0 WeMos D1 Mini // Receiver on interrupt 0 => that is pin #2
  mySwitch.enableTransmit(D2); //D2->GPIO4 WeMos D1 Mini
  mySwitch.setProtocol(etek_protocol);
  mySwitch.setPulseLength(etek_PulseLength);
  mySwitch.setRepeatTransmit(3);

  pinMode(led_outputPin, OUTPUT);
  
  power(false, F("Power OFF"), false);
  setup_server();
  // Fauxmo
#ifdef ENABLE_WEMO
  fauxmo.addDevice(WEMO_DEV);
  fauxmo.onMessage([](unsigned char device_id, const char * device_name, bool state) {
    if (strcmp(device_name, WEMO_DEV) == 0) 
      power_delayed = state?1:0;
    //power(state, state?F("WeMo ON"):F("WeMo OFF"), true);
    //Serial.printf("[MAIN] Device #%d (%s) state: %s\n", device_id, device_name, state ? "ON" : "OFF");
  });
#endif
  pinMode(ac_inputPin, INPUT);      // set pin as input
  attachInterrupt(ac_inputPin, []()
    { 
      AC_millis = _millis;
      if (AC_periods<AC_periods_min) 
        AC_periods++;
    }, RISING);
}

MDNSResponder mdns;

bool check_connection()
{
  static uint32_t timeout_millis = 0;
  int status = WiFi.status();
  if (status == WL_CONNECTED) {
    if (timeout_millis>0) {
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      timeout_millis = 0;
      if (mdns.begin(HOST_MDNS, WiFi.localIP())) {
        Serial.println("MDNS responder started");
      }
    }
    return true;
  }
  if (timeout_millis) {
    if (_millis>timeout_millis) {
      timeout_millis = 0;
      //retry_millis = _millis+RETRY_CONNECT*1000;
      Serial.println(F("Retry"));
    }
    return false;
  }
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Connecting to '%s'\n", WIFI_SSID);
  timeout_millis = _millis+RETRY_CONNECT*1000;
  return false;
}

void loop() {
  //while (1)
  {
    _millis = millis();
    if (check_connection()) {
    }
#ifdef ENABLE_WEMO
    fauxmo.handle();
#endif
    // process AC optocouple
    if (AC_on) {
      if (abs(_millis-AC_millis)>AC_timeout) {
        AC_periods = 0;
        AC_on = false;
        power(false, F("AC ON"), true);
      }
    } else {
      if (AC_periods>=AC_periods_min) {
        AC_on = true;
        power(true, F("AC OFF"), true);
      }    
    }
    // process web requests
    if (power_delayed >= 0) {
      bool on = (power_delayed>0);
      power_delayed = -1;
      power(on, on?F("Web ON"):F("Web OFF"), true);
    }
    // process web requests
    if (mySwitch.available()) {
      unsigned long code    = mySwitch.getReceivedValue();
      unsigned int bits     = mySwitch.getReceivedBitlength();
      unsigned int delay    = mySwitch.getReceivedDelay();
      unsigned int protocol = mySwitch.getReceivedProtocol();
      mySwitch.resetAvailable();
      if (code == 0) {
        Serial.println(F("Unknown encoding."));
      } else if (etek_protocol==protocol &&
                 etek_lengthBit==bits &&
                 delay>etek_PulseLength*85/100 && 
                 delay<etek_PulseLength*115/100) {
        switch (code) {
        case etek_5_on:
          power(true, F("received ETEK code ON"), false);
          break;
        case etek_5_off:
          power(false, F("received ETEK code OFF"), false);
          break;
        default:
          Serial.printf("ETEK code Hex: 0x%lX Decimal: %ld\n", code, code);
        }
      } else {
        Serial.printf("Hex:0x%lX Decimal:%ld Bits:%d PulseLength: %duS Protocol:%d\n", code, code, bits, delay, protocol);
      }
    }
  }
}

