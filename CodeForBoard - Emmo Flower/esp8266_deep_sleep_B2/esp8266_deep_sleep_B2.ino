// Include the ESP8266 WiFi library. (Works a lot like the
// Arduino WiFi library.)
#include <ESP8266WiFi.h>
#include <DHT.h>
#include <SparkFunTSL2561.h>
#include <Wire.h>           

//needed for library
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <WiFiManager_mod.h>

#define DEBUG true

/////////////////////
// Pin Definitions //
/////////////////////
#define DHTPIN 4            // what digital pin we're connected to
#define DHTTYPE DHT22       // DHT 22  (AM2302), AM2321
//#define LED_PIN  5          // Thing's onboard, green LED
#define LED_PIN_B  2        // ESP-12E onboard, blue LED, don't use this one - in collision with light sensor
#define ANALOG_PIN A0       // The only analog pin on the Thing
#define MOISTURE_ON_PIN 12  //
#define CLEAR_CONFIG_PIN 13 // When low on reset clear configuration

//////////////////////
// WiFi Definitions //
//////////////////////
const char WiFiSSID[] = "5ku11";      // home SSID
const char WiFiPSK[] = "8R5GM39L";    // home pass

String macID = "";

//////////////////////////
// DirektnoIzBaste keys //
//////////////////////////
const char dizbHost[] = "masinealati.rs";

/////////////////
// Post Timing //
/////////////////
const unsigned long postRate = 30000;   // How often do we read sensors and post values to the web server
unsigned int retryCount = 3;           // How many times trying to send before going to deep sleep

//////////////////////////////////////////////////////////////////////////////////
// DHT SENSOR Initialize                                                        //
//////////////////////////////////////////////////////////////////////////////////
// Note that older versions of this library took an optional third parameter to //
// tweak the timings for faster processors.  This parameter is no longer needed //
// as the current DHT reading algorithm adjusts itself to work on faster procs. // 
//////////////////////////////////////////////////////////////////////////////////
DHT dht(DHTPIN, DHTTYPE);

////////////////////////////////////////////////////////
// LIGHT SENSOR                                       //
// Create an SFE_TSL2561 object, here called "light": //
////////////////////////////////////////////////////////
SFE_TSL2561 light;
boolean gain;     // Gain setting, 0 = X1, 1 = X16;
unsigned int ms;  // Integration ("shutter") time in milliseconds

WiFiManager wifiManager;
boolean enter_configure_mode = false;
boolean _debug = true;
WiFiServer server(80);

String _ssid = "";
String _pass = "";

////////////////////////////////////////////////
// SETUP - executes one time on program start //
////////////////////////////////////////////////

void setup() 
{
  initModule();
  initSensors();
  connectWiFi();
}

void loop() 
{
  if (postToDizB()){
    ESP.deepSleep(postRate * 1000, WAKE_RF_DEFAULT); // Sleep for 30 seconds
  }else{
    if (DEBUG)
      Serial.println("failed to send, trying in 10 sec");
    retryCount = retryCount - 1;
    if (retryCount > 0){
      delay(10000); // sleep for 10 sec
    }else{
      ESP.deepSleep(postRate * 1000, WAKE_RF_DEFAULT); // Sleep for 30 seconds
    }
  }
}

//////////////////////////////////////////////
// initModule                               //
// configure pins, gets module MAC address  //
//////////////////////////////////////////////

void initModule()
{
  if (DEBUG)
    Serial.begin(9600);
  if (DEBUG)
    Serial.println(ESP.getResetInfo());
  pinMode(MOISTURE_ON_PIN, OUTPUT);
  digitalWrite(MOISTURE_ON_PIN, LOW);
  //pinMode(LED_PIN, OUTPUT);
  //digitalWrite(LED_PIN, LOW);

  pinMode(CLEAR_CONFIG_PIN, INPUT);  // default value is LOW
  if (digitalRead(CLEAR_CONFIG_PIN) == HIGH){
    int low_count = 0;
    for (int i = 0; i<3; i++)
    {
      if (digitalRead(CLEAR_CONFIG_PIN) == HIGH){
        low_count++;
        delay(200);
      }else{
        break;
      }
    }
    if (low_count > 2){
      // reset detected
      enter_configure_mode = true;
    }
  }
  // Don't need to set ANALOG_PIN as input, 
  // that's all it can be.

}

//////////////////////////////////////////////
// initSensors                              //
//   //
//////////////////////////////////////////////

void initSensors(){
  dht.begin();
  light.begin();
  // If gain = false (0), device is set to low gain (1X)
  // If gain = high (1), device is set to high gain (16X)

  gain = 0;

  // If time = 0, integration will be 13.7ms
  // If time = 1, integration will be 101ms
  // If time = 2, integration will be 402ms
  // If time = 3, use manual start / stop to perform your own integration

  unsigned char time = 3; // Let's have our own integration

  // setTiming() will set the third parameter (ms) to the
  // requested integration time in ms (this will be useful later):

  light.setTiming(gain,time,ms);
  light.setPowerUp();
}

//////////////////////////////////////////////
// connectWifi                              //
//   //
//////////////////////////////////////////////

void connectWiFi()
{
  
  //wifiManager.resetSettings(); // clear saved WiFi credentials
  
  // Do a little work to get a unique-ish name. Append the
  // last two bytes of the MAC (HEX'd) to "Thing-":
  uint8_t mac[WL_MAC_ADDR_LENGTH];
  WiFi.softAPmacAddress(mac);
  String ss = String(mac[WL_MAC_ADDR_LENGTH - 2], HEX) +
                 String(mac[WL_MAC_ADDR_LENGTH - 1], HEX);
  ss.toUpperCase();
  
  String AP_NameString = "SENZOR-" + ss;
  
  for (int i=0; i< WL_MAC_ADDR_LENGTH; i++){
    if (mac[i] < 16){
      macID = macID + "0";
    }
    macID = macID + String(mac[i], HEX);
  }
  macID.toUpperCase();

  char AP_NameChar[AP_NameString.length() + 1];
  memset(AP_NameChar, 0, AP_NameString.length() + 1);

  for (int i=0; i<AP_NameString.length(); i++)
    AP_NameChar[i] = AP_NameString.charAt(i);
  //first parameter is name of access point, second is the password
  
  if (enter_configure_mode){
    WiFi.disconnect(); 
    WiFi.mode(WIFI_AP);
    printDBG("SET AP");
    WiFi.softAP(AP_NameChar, "12345678");
    delay(500); // Without delay I've seen the IP address blank
    printDBG("AP IP address: ");
    printDBG(toStringIp(WiFi.softAPIP()));
    server.begin(); // Web server start
    printDBG("HTTP server started");
    blinkLed(1000, 2);
    // waiting for client
    unsigned long start = millis();
    unsigned long timeout = 180 * 1000;
    WiFiClient client;
    while (millis() < start + timeout){
      client = server.available();
      if (client) break;
    }
    if (!client){
      printDBG("Configure timeout...sleep forever.");
      ESP.deepSleep(0, WAKE_RF_DEFAULT); // sleep forever
    }else{
      printDBG("Client connected...");
      // Read the first line of the request
      String req = client.readStringUntil('\r');
      printDBG(req);
      client.flush();
      //http://192.168.4.1/wifisave?s=%1$s&p=%2$s
      // get ssid and password
      // try to connect...
      if (req.indexOf("/wifisave")!=-1){
        int startSSID = req.indexOf("?s=") + 3;
        int endSSID = req.indexOf("&p=");
    
        int startPWD = req.indexOf("&p=") + 3;
        int endPWD = req.lastIndexOf(" ");
    
        _ssid = req.substring(startSSID, endSSID);
        _pass = req.substring(startPWD, endPWD);
        printDBG("Connecting to:");
        printDBG(_ssid);
        printDBG("PASS:");
        printDBG(_pass);

        String data = "{success:true}";
        // Prepare the response. Start with the common header:
        String s = "HTTP/1.1 200 OK\r\n";
        s += "Content-Type: text/html\r\n";
        s += "Content-Length: " + String(data.length()) + "\r\n\r\n";
        s += data;
      
        // Send the response to the client
        client.print(s);
        delay(1);
        Serial.println("Client disonnected");
      }
      
    }
    
  }
  
  //wifiManager.connectWifi("","");
//  if (!wifiManager.autoConnect(AP_NameChar, "12345678")){
//    blinkLed();
//    if (DEBUG)
//      Serial.println("Failed to connect...going to deep sleep mode.");
//    ESP.deepSleep(postRate * 1000, WAKE_RF_DEFAULT); // restart in forever
//  }
  //wifiManager.resetSettings();
  // CONNECT TO WiFi using stored credentials
  WiFi.mode(WIFI_STA);
  if (_ssid!= "")
    WiFi.begin(_ssid.c_str(), _pass.c_str());
  else
    WiFi.begin();
  int connRes = WiFi.waitForConnectResult();
  if (connRes != WL_CONNECTED){
    printDBG("Failed to connect to WiFi with stored credentials");
    blinkLed(250, 20);
    printDBG("Sleeping...");
    ESP.deepSleep(postRate * 1000, WAKE_RF_DEFAULT); // Try again later
  }else{
    printDBG("Connected to WiFi successfully...");
  }
  
}

int postToDizB()
{

  //return 1;
  
  delay(2000); // Let DHT sensor fully prepare before reading, try lower value
  
  // LED turns on when we enter, it'll go off when we 
  // successfully post.
  //digitalWrite(LED_PIN, HIGH);

  // Declare an object from the Phant library - phant
 // Phant phant(PhantHost, PublicKey, PrivateKey);


 // read moisture sensor
  digitalWrite(MOISTURE_ON_PIN, HIGH);
  int moist = analogRead(ANALOG_PIN); // 0 to 1023
  //double moist_scaled = moist/1023.0 * 10; // 0 to 10
  //moist_scaled = moist_scaled + 40; // 40 to 50
  String moisture = String(moist);
  digitalWrite(MOISTURE_ON_PIN, LOW);

  if (DEBUG){
      Serial.print("Moisture sensor: ");
      Serial.print(moist);
      Serial.println();
  }
  String postedID = macID;

  // Reading temperature or humidity takes about 250 milliseconds!
  // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
  float h = dht.readHumidity();
  // Read temperature as Celsius (the default)
  float t = dht.readTemperature();
  // Read temperature as Fahrenheit (isFahrenheit = true)
  float f = dht.readTemperature(true);

  // Check if any reads failed and exit early (to try again).
  if (isnan(h) || isnan(t) || isnan(f)) {
    if (DEBUG)
      Serial.println("Failed to read from DHT sensor!");
    return 0;
   }

  // Compute heat index in Fahrenheit (the default)
  float hif = dht.computeHeatIndex(f, h);
  // Compute heat index in Celsius (isFahreheit = false)
  float hic = dht.computeHeatIndex(t, h, false);
  
  // Reading light sensor
  ms = 402;
  light.manualStart();
  delay(ms);
  light.manualStop();

  // There are two light sensors on the device, one for visible light
  // and one for infrared. Both sensors are needed for lux calculations.
  
  // Retrieve the data from the device:

  unsigned int data0, data1;
  double lux;    // Resulting lux value
  boolean good;  // True if neither sensor is saturated
  
  if (light.getData(data0,data1))
  {
    // getData() returned true, communication was successful
    if (DEBUG){
      Serial.print("data0: ");
      Serial.print(data0);
      Serial.print(" data1: ");
      Serial.print(data1);
    }
    // To calculate lux, pass all your settings and readings
    // to the getLux() function.
    
    // The getLux() function will return 1 if the calculation
    // was successful, or 0 if one or both of the sensors was
    // saturated (too much light). If this happens, you can
    // reduce the integration time and/or gain.
    // For more information see the hookup guide at: https://learn.sparkfun.com/tutorials/getting-started-with-the-tsl2561-luminosity-sensor
    
    // Perform lux calculation:

    good = light.getLux(gain,ms,data0,data1,lux);
    
    // Print out the results:
    if (DEBUG){
      Serial.print(" lux: ");
      Serial.print(lux);
      if (good) Serial.println(" (good)"); else Serial.println(" (BAD)");
    }
  }
  else
  {
    if (DEBUG)
      Serial.println("Failed to read from Ligtht sensor!");
    return 0;
  }

  // Now connect to data.sparkfun.com, and post our data:
  WiFiClient client;
  const int httpPort = 80;
  if (!client.connect(dizbHost, httpPort)) 
  {
    // If we fail to connect, return 0.
    if (DEBUG)
      Serial.println("Failed to connect to server.");
    return 0;
  }
  //String analogValue0 = String(analogRead(A0), DEC);

  String temperature  = String(t);
  //String temperature  = String(25);
  String humidity = String(h);
  //String humidity = String(50);
  String light_lux = String(lux);
  String tsData = "temperature="+ temperature  + ";humidity=" + humidity + ";luminosity=" + light_lux + ";moisture="+ moisture + ";ID=" + postedID;
  // If we successfully connected, print our thigspeak post:
  client.print("POST /parametri.php?action=osnovniparametri HTTP/1.1\n");
  client.print("Host: masinealati.rs\n");
  client.print("Connection: close\n");
  //client.print("X-THINGSPEAKAPIKEY: "+writeAPIKey+"\n");
  client.print("Content-Type: application/x-www-form-urlencoded\n");
 // client.print("Content-Type: */*\n");
  client.print("Content-Length: ");
  client.print(tsData.length());
  client.print("\n\n");

  client.print(tsData);


  delay(300);
  // Read all the lines of the reply from server and print them to Serial
  while(client.available()){
    String line = client.readStringUntil('\r');
    if (DEBUG)
      Serial.print(line); // Trying to avoid using serial
  }

  // Before we exit, turn the LED off.
  //digitalWrite(LED_PIN, LOW);

  return 1; // Return success
}

void blinkLed(int del, int count){
  pinMode(LED_PIN_B, OUTPUT);
  int i = 0;
  while(i<count){
    digitalWrite(LED_PIN_B, HIGH);
    delay(del);
    digitalWrite(LED_PIN_B, LOW);
    delay(del);
    i++;
  }
}

////////////////////////////////////
// A Routine to print debug text
////////////////////////////////////
void printDBG(String text) {
  if (_debug) {
    Serial.print("*DBG: ");
    Serial.println(text);
  }
}

String toStringIp(IPAddress ip) {
  String res = "";
  for (int i = 0; i < 3; i++) {
    res += String((ip >> (8 * i)) & 0xFF) + ".";
  }
  res += String(((ip >> 8 * 3)) & 0xFF);
  return res;
}



