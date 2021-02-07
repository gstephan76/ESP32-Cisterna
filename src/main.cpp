// ESP32

#define MQTT_MAX_PACKET_SIZE 512 // MUST be defined before the #include <PubSubClient.h> \
                                 // it doesn't work in Arduino! It must be defined in <PubSubClient.h>

#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>
//#include <iostream>+
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
//#include <Esp.h> // Needed for EPS.restart();
#include <esp_int_wdt.h>
#include <esp_task_wdt.h>
#include <Adafruit_SPIDevice.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <soc/rtc_wdt.h>
#include <esp_log.h>

//#undef DEBUG
#define DEBUG

#define NEW_RECONNECT
//#define DEBUG
#define VERSION "ESP32 - CISTERNA - 1.1"

#define MAX_JSON 480
static const size_t numChars = MAX_JSON + 1; // Max Lenght of each data unit
static char receivedChars[numChars];
//static char tempChars[numChars];        // temporary array for use by strtok() function
static size_t maxJsonSize = numChars;

// variables to hold parsed data
static char messageFromSerial[numChars] = {0};
static int integerFromSerial = 0;
static float floatFromSerial = 0.0;
static unsigned long seq = 0;

static boolean newData = false;
static unsigned long windowTimer = 0;

// use for no hardreset at the fist loop
bool newstart = 0;

#define MQTT_SERVER "192.168.1.8"
#define MQTT_PORT 1883
//#define OUTTOPIC "cisterna/main/out"

//const char* ssid = "BGP";
const char *ssid = "Spirit";
const char *password = "vininautos47";
//const char* mqtt_server = "192.168.1.59";

const char *inTopic = "cisterna/main/in";
const char *outTopic = "cisterna/main/out";

static bool isJason = false;

// Callback function header
void callback(char *topic, byte *payload, unsigned int length);

WiFiClient wifiClient;

byte reconnect();
void sendJson(const char *sensor, const int value1, const float value2, unsigned long seq = 0, const String &error = "");
void sendKeepAlive(const char *sensor, unsigned long seq);
void jasonSendException(const char *sensor, const char *msg, unsigned long seq = 0);
String macToStr(const uint8_t *mac);

PubSubClient client(MQTT_SERVER, MQTT_PORT, callback, wifiClient);

// ===================================

void hard_restart()
{
  esp_task_wdt_init(1, true);
  esp_task_wdt_add(NULL);
  while (true)
    ;
}

// ===================================

void WIFI_Connect()
{
  long wifidelay = 0;
  for (int k = 0; k < 25; k++)
  {
    if (WiFi.status() != WL_CONNECTED)
    {
      wifidelay += 500;
      WiFi.disconnect();
      rtc_wdt_feed();
      delay(wifidelay);
#ifdef DEBUG
      Serial.print(">>> ----- Connecting to WiFi... ---- <<<, delay = ");
      Serial.println(wifidelay);
#endif
      WiFi.mode(WIFI_STA);
      rtc_wdt_feed();
      delay(wifidelay);
      WiFi.begin(ssid, password);
      rtc_wdt_feed();
      delay(wifidelay);
      //delay(wifidelay);
    }
  }
  if (WiFi.status() != WL_CONNECTED)
  {
#ifdef DEBUG
    Serial.println("Connecting to WiFi has failed!");
#endif
    delay(5000);
    hard_restart();
    // I think that is no longer necessary, but it does not hurt either.
    delay(2000);
  }
  if (WiFi.status() == WL_CONNECTED)
  {
#ifdef DEBUG
    Serial.println("");
    Serial.println("WiFi Connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
#endif
  }
}

// ==========================================

size_t recvWithStartEndMarkers()
{
  static boolean recvInProgress = false;
  static size_t ndx = 0;
  size_t msgSize = 0;

  const char startMarker = '<';
  const char endMarker = '>';
  char startMarkerJson = '$';
  //char startMarkerJson = '{';
  //char endMarkerJson = '}';
  const char endMarkerJson = '#';
  char rc;

  //memset(receivedChars, '\0', sizeof(receivedChars));

#ifdef DEBUG
  Serial.println("> recvWithStartEndMarkers() ");
#endif

  while (Serial.available() > 0 && newData == false)
  {
    yield(); // Just to be safe in case there is a lot of data in the serial buffer; run the ESP WIFI functions

    rc = Serial.read();

    if (recvInProgress == true)
    {
      if (ndx >= numChars - 3)
      {
        // Msg is too long, terminating...
        ndx = numChars - 3;
        msgSize = ndx;
        ndx = 0;

        //memset(receivedChars, '\0', sizeof(receivedChars));
        receivedChars[ndx] = '\0';
        recvInProgress = false;
        newData = true;

        client.publish(outTopic, "Received message too long... Clipping here...");
        break;
        // Since there is no more room to store chars
      }
      if ((rc != endMarker) && (rc != endMarkerJson))
      {
        // New char within the message
        receivedChars[ndx] = rc;
        ndx++;
      }
      else if ((rc == startMarkerJson))
      {
        // Received new message without properly terminating the current one
        receivedChars[ndx] = '\0'; // terminate the string
        //memset(receivedChars, '\0', sizeof(receivedChars));
        recvInProgress = false;
        newData = true;
        msgSize = ndx;
        ndx = 0;

        client.publish(outTopic, "Received a new serial message without proper termination on the current one ...");
      }
      else if ((rc == endMarkerJson))
      {
        // Received termination char
        receivedChars[ndx] = '\0'; // terminate the string
        recvInProgress = false;
        newData = true;
        msgSize = ndx;
        ndx = 0;
#ifdef DEBUG
        Serial.print("  MSG_SIZE = ");
        Serial.println(msgSize);
        Serial.print("  RECC = ");
        Serial.println(rc);
#endif
      }
      else if ((rc == endMarker))
      {
        receivedChars[ndx] = '\0'; // terminate the string
        recvInProgress = false;
        msgSize = ndx;
        ndx = 0;
        newData = true;
#ifdef DEBUG
        Serial.println(" Finalizing <> msg ... ");
        Serial.print("  MSG_SIZE = ");
        Serial.println(msgSize);
        Serial.print("  RECC = ");
        Serial.println(rc);
#endif
      }
      else
      {
        receivedChars[ndx] = '\0'; // terminate the string
        recvInProgress = false;
        msgSize = ndx;
        ndx = 0;
        newData = true;
#ifdef DEBUG
        Serial.println(" ------------------->>> ASSERT <<<------------------ ");
        Serial.print("  MSG_SIZE = ");
        Serial.println(msgSize);
        Serial.print("  RECC = ");
        Serial.println(rc);
#endif
      }
    }
    else if ((rc == startMarker) || (rc == startMarkerJson))
    {
      recvInProgress = true;

#ifdef DEBUG
      Serial.print("  RC in ");
      Serial.println(rc);
#endif
      if (rc == startMarkerJson)
      {
        isJason = true;
#ifdef DEBUG
        Serial.println("RECV -> IS JSON");
#endif
      }
      else
      {
        isJason = false;
      }
    }
    else
    {
#ifdef DEBUG
      Serial.print("Garbagge received: ");
      Serial.println(rc);
#endif
    }
  }
#ifdef DEBUG
  Serial.print("  RecInProgress = ");
  Serial.println(recvInProgress);
  Serial.print("  New data = ");
  Serial.println(newData);
  Serial.print("  isJson = ");
  Serial.println(isJason);
  Serial.print("  receivedChars = ");
  Serial.println(receivedChars);
  Serial.print("  msgSize = ");
  Serial.println(msgSize);
  Serial.println("< recvWithStartEndMarkers() ");
#endif
  if (msgSize == 0)
  {
    return (numChars - 3);
  }
  else
  {
    return msgSize;
  }
}

//============

void parseSerialData(char *serialCharBuffer)
{

  // split the data into its parts
  char *strtokIndx; // this is used by strtok() as an index

  strtokIndx = strtok(serialCharBuffer, ","); // get the first part - the string
  strcpy(messageFromSerial, strtokIndx);      // copy it to messageFromSerial

  strtokIndx = strtok(NULL, ",");       // this continues where the previous call left off
  integerFromSerial = atoi(strtokIndx); // convert this part to an integer

  strtokIndx = strtok(NULL, ",");
  floatFromSerial = atof(strtokIndx); // convert this part to a float
}

//============

void showParsedSerialData()
{
  Serial.print("Message ");
  Serial.println(messageFromSerial);
  Serial.print("Integer ");
  Serial.println(integerFromSerial);
  Serial.print("Float ");
  Serial.println(floatFromSerial);
}

void setup()
{
  newstart = 1;

  delay(100);
  Serial.begin(9600);
  delay(100);

  Serial.println(VERSION);

#ifdef DEBUG
  Serial.print("\n\n Initializing...");
#endif

  WiFi.mode(WIFI_STA);
  // important delay, it doesn't send data to the server without it (only needed if using lwIP v2)
  rtc_wdt_feed();
  delay(4000);
  //WiFi.begin(ssid, password);

  while (!client.connected())
  {
    if (!reconnect())
    {
      rtc_wdt_feed();
      delay(5000);
    }
  }

  randomSeed(micros());
  delay(100);

  // Initialize relays as OFF, just in case there was junk in the serial port
  {
    int i;
    String str;

    Serial.println("");

    for (i = 5; i <= 12; i++)
    {
      str = "<s," + String(i) + ",0.0>";
      Serial.println(str);
      delay(25);
    }
  }

  {
    // INIT Message
    String tmp = String("Init msg from: ") +
                 WiFi.localIP().toString() +
                 String(" - Version: ") +
                 String(VERSION);
    char buf[maxJsonSize];
    tmp.toCharArray(buf, tmp.length() + 1);
    sendKeepAlive(buf, seq);
  }
}

void loop()
{
  char tempChars[numChars];

  if (!client.connected() || WiFi.status() != WL_CONNECTED)
  {
    while (!client.connected())
    {
      if (!reconnect())
      {
        rtc_wdt_feed();
        delay(5000);
      }
    }
  }

  // Maintain MQTT connection
  client.loop();

  // MUST delay to let ESP8266 WiFI functions to run
  delay(50);

#ifdef DEBUG
  Serial.println("");
  rtc_wdt_feed();
  delay(2000);
#endif

  {
    // Receiving data from Serial
    size_t n = recvWithStartEndMarkers();

    if (newData == true)
    {

      if (isJason == false)
      {
        // Process received data, which is in <> format
#ifdef DEBUG
        Serial.print("received <> format ...");
        Serial.println(receivedChars);
#endif
        //strcpy(tempChars, receivedChars);
        // this temporary copy is necessary to protect the original data
        //   because strtok() replaces the commas with \0
        /* parseSerialData(tempChars); */
        parseSerialData(receivedChars);
        //showParsedSerialData();
        sendJson(messageFromSerial, integerFromSerial, floatFromSerial, seq);
        newData = false;
        isJason = false;
        memset(receivedChars, '\0', sizeof(receivedChars));
        seq++;
      }
      else
      {
        // Data is already in JSON format, so just relay the data
#ifdef DEBUG
        Serial.print("JSON = ");
        Serial.print(receivedChars);
        Serial.println("|");
#endif
        //char buf2[maxJsonSize];
        //memset(buf2, '\0', sizeof(buf2));

        //strncat(buf2, "{", 1);
        //strcat(buf2, receivedChars);
        //strncat(buf2, "}", 1);
#ifdef DEBUG
        Serial.print("\t publish size:: ");
        Serial.println(n);
#endif
        int ret = client.publish(outTopic, receivedChars, n);

#ifdef DEBUG
        //Serial.print("BUF2 = ");
        //Serial.println(buf2);
        Serial.print("cond = ");
        Serial.println(ret);
#endif
        newData = false;
        isJason = false;
        seq++;
        memset(receivedChars, '\0', sizeof(receivedChars));
      }
    }
    else
    {
#ifdef DEBUG
      Serial.println("New Data false ...");
#endif
    }
    // Sending data
    delay(5); // Time for ESP WIFI functions to run

    // Keep alive
    {
      if (((millis() - windowTimer)) > 120000)
      {
        String tmp = String("Keep alive: ") + WiFi.localIP().toString();
        char buf[maxJsonSize];
        tmp.toCharArray(buf, tmp.length() + 1); //FIXME: Do we need +1 , once the output is being clipped???
        sendKeepAlive(buf, seq);
        windowTimer = millis();
        seq++;
      }
    }
  }

  if (seq >= 4294967293)
  { // MAX ULONG 4,294,967,295 (2^32 - 1)
    seq = 1000;
  }

  newstart = 0;
}

void sendKeepAlive(const char *sensor, unsigned long seq /* = 0 */)
{

  StaticJsonDocument<MAX_JSON + 1> doc;
  //JsonObject& root = jsonBuffer.createObject();

  doc["sensor"] = sensor;
  doc["seq"] = seq;
  doc["rssi"] = WiFi.RSSI();

  char strObj[maxJsonSize];
  //root.printTo(strObj, sizeof(strObj));
  size_t n = serializeJson(doc, strObj, sizeof(strObj));
#ifdef DEBUG
  Serial.print("\t publish size:: ");
  Serial.println(n);
#endif

  client.publish(outTopic, strObj, n);
}

void jasonSendException(const char *sensor, const char *msg, unsigned long seq /* = 0 */)
{

  StaticJsonDocument<MAX_JSON + 1> doc;
  //JsonObject& root = jsonBuffer.createObject();

  doc["sensor"] = sensor;
  doc["seq"] = seq;
  doc["rssi"] = WiFi.RSSI();
  doc["exception"] = msg;

  char strObj[maxJsonSize];
  //root.printTo(strObj, sizeof(strObj));
  size_t n = serializeJson(doc, strObj, sizeof(strObj));
#ifdef DEBUG
  Serial.print("\t publish size:: ");
  Serial.println(n);
#endif

  client.publish(outTopic, strObj, n);
}

void sendJson(const char *sensorName,
              const int value1,
              const float value2,
              unsigned long seq /* = 0 */,
              const String &error /* = "" */)
{

  StaticJsonDocument<MAX_JSON + 1> doc;
  JsonObject sensor = doc.createNestedObject("snr");

  sensor["name"] = sensorName;
  sensor["seq"] = seq;
  sensor["err"] = error;

  //JsonArray& data = root.createNestedArray("data");
  /* JsonArray array = doc.as<JsonArray>();
       JsonArray nested = array.createNestedArray();

       nested.add(value1);
       nested.add(value2);
       nested.add(value3); */

  // Add the "value" array
  JsonArray values = doc.createNestedArray("values");

  JsonObject val1 = values.createNestedObject();
  val1["key"] = "pin";
  val1["val"] = value1;
  JsonObject val2 = values.createNestedObject();
  val2["key"] = "val";
  val2["val"] = value2;

  char strObj[maxJsonSize];
  //root.printTo(strObj, sizeof(strObj));
  size_t n = serializeJson(doc, strObj, sizeof(strObj));
#ifdef DEBUG
  Serial.print("\t publish size: ");
  Serial.println(n);
#endif

  client.publish(outTopic, strObj, n);
}

void callback(char *topic, byte *payload, unsigned int lenght)
{

  //convert topic to string to make it easier to work with
  //String topicStr = topic;

  char message_buff[numChars + 5];
  //int i;

  if (lenght > numChars)
    lenght = numChars;
  //char * message_buff = (char *) payload;
  memcpy(message_buff, payload, lenght);

  message_buff[lenght] = '\0';

  if ((message_buff[0] != '<') || (message_buff[lenght - 1] != '>'))
  {
    client.publish(outTopic, "Invalid format: abssence of either '<' or '>'", lenght);
    return;
  }

  // Relays message to Serial without parsing it
  Serial.println(message_buff);
  //Serial.flush();
  //delay(20);
}

/* New improved Reconnect to ESP32, once the old one hangs while
 * trying to reconnect to the mqtt broker
 *
 */

#ifdef NEW_RECONNECT

byte reconnect()
{

#ifdef DEBUG
  Serial.println("\n !!!!! NEW RECONNECT: mqtt reconnect start !!!!!");
#endif
  // test is mqtt connected
  if (!client.connected())
  {

    if (newstart == 0)
    {
      hard_restart();
    }
    // test is wifi connected
    if (WiFi.status() != WL_CONNECTED)
    {
      WIFI_Connect();
    }

#ifdef DEBUG
    Serial.println("Attempting MQTT connection...");
#endif
    String clientName;
    clientName = "esp32-cisterna";
    uint8_t mac[6];
    WiFi.macAddress(mac);
    clientName += macToStr(mac);

#ifdef DEBUG
    Serial.print("\t Client name: ");
    Serial.print(clientName);
#endif

    // insert unique id
    if (client.connect((char *)clientName.c_str()))
    {
#ifdef DEBUG
      Serial.println("connected");
#endif
      newstart = 0;

      String tmp = String("ESP32 - Cisterna - Reconnecting ...");

      char buf[maxJsonSize];
      tmp.toCharArray(buf, tmp.length() + 1);
      jasonSendException("Init", buf, seq);

      //client.publish(outTopic, "ESP32 - Cisterna - Reconnecting ...");
      client.subscribe(inTopic);
    }
    else
    {
#ifdef DEBUG
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" wait 0.5 seconds");
#endif
      delay(500);
      return false;
    }
  }

#ifdef DEBUG
  Serial.println("mqtt reconnect end...");
#endif
  return true;
}

#elif
// OLD Reconnect that works on ESP8266

byte reconnect()
{
  short wifiCounter = 0;

  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  // important delay, it doesn't send data to the server without it (only needed if using lwIP v2)
  rtc_wdt_feed();
  delay(4000);
#ifdef DEBUG
  Serial.println("Before Wifi begin()...");
#endif
  WiFi.begin(ssid, password);
  int count = 0;

  // Attempt to reconnect if the connection is lost
  if (WiFi.status() != WL_CONNECTED)
  {

#ifdef DEBUG
    Serial.println();
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);
#endif
    /* Explicitly set the ESP8266 to be a WiFi-client, otherwise, it by default,
           would try to act as both a client and an access-point and could cause
           network-issues with your other WiFi-devices on your WiFi-network. */
    // WiFi.mode(WIFI_STA);

    while (WiFi.status() != WL_CONNECTED)
    {
#ifdef DEBUG
      Serial.print(".");
#endif
      rtc_wdt_feed();
      delay(1000);
      wifiCounter++;
      if (wifiCounter >= 300)
      {
        ESP.restart(); // Reboot if it is taking more than 5 mins.
      }
    }

    //randomSeed(micros());

#ifdef DEBUG
    Serial.println("");
    Serial.println("WiFi connected!!!");
    Serial.println("My IP address: ");
    Serial.println(WiFi.localIP());
#endif
  }

  // make sure we are connected to WIFI before connecting to MQTT
  if (WiFi.status() == WL_CONNECTED)
  {
    // loop until we are reconnected to the MQTT server

    while (!client.connected())
    {
      // Generate client name based on MAC address and the last 8 bits of the microsecond counter
      String clientName;

      clientName = "esp32-cisterna";
      uint8_t mac[6];
      WiFi.macAddress(mac);
      clientName += macToStr(mac);
#ifdef DEBUG
      Serial.print("\t Client name: ");
      Serial.print(clientName);
#endif

      // if connected, subscribe to the topic (s) we want to be notified about
      if (client.connect((char *)clientName.c_str()))
      {
#ifdef DEBUG
        Serial.print("\t MQTT connected ...");
#endif
        client.subscribe(inTopic);
      }
      else
      {
        Serial.print("\t MQTT Connection Failed ...");
        Serial.print("failed, rc=");
        Serial.print(client.state());
        Serial.println(" try again in 5 seconds");
        // Wait 5 seconds before retrying

        count++;

        // Got stuck trying to connect to MQTT without a WiFi connection ...
        if (count >= 30)
        {
          return false;
        }
        rtc_wdt_feed();
        delay(5000);
      }
    }
  }
  else
  {
    // After all it is not connected to Wi-Fi
    return false;
  }
  return true;
}

#endif

// Generate unique name from MAC addr
String macToStr(const uint8_t *mac)
{
  String result;

  for (int i = 0; i < 6; i++)
  {
    result += String(mac[i], 16);

    if (i < 5)
    {
      result += ':';
    }
  }

  return result;
}