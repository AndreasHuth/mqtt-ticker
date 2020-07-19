#include <ArduinoOTA.h>

#include <ESP8266WiFi.h>


#include <PubSubClient.h>
#include <WiFiManager.h>                    //https://github.com/tzapu/WiFiManager
#include <MD_MAX72xx.h>
#include <SPI.h>

#define PRINT_CALLBACK  0
#define DEBUG 0
#define LED_HEARTBEAT 0

#if DEBUG
#define PRINT(s, v) { Serial.print(F(s)); Serial.print(v); }
#define PRINTS(s)   { Serial.print(F(s)); }
#else
#define PRINT(s, v)
#define PRINTS(s)
#endif

#if LED_HEARTBEAT
#define HB_LED  D4
#define HB_LED_TIME 500 // in milliseconds
#endif

// Define the number of devices we have in the chain and the hardware interface
// NOTE: These pin numbers will probably not work with your hardware and may
// need to be adapted
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW // PAROLA_HW
#define MAX_DEVICES 4

#define CLK_PIN   D5 // or SCK
#define DATA_PIN  D7 // or MOSI
#define CS_PIN    D6 // or SS

// SPI hardware interface
MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);
// Arbitrary pins
//MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

const char* ssid      = "SSID";
const char* password  = "PASSWORD";

const char* mqtt_server = "192.168.0.192";
// const char* device_name = "ESP_MATRIX";

const char* pup_alive        = "/matrix/active";
const char* sub_value        = "/matrix/value";
const char* sub_speed        = "/matrix/speed";
const char* sub_intensity    = "/matrix/intensity";
const char* sub_OnOff        = "/matrix/state";

WiFiClient espClient;
PubSubClient client(espClient);

// Global message buffers shared by Wifi and Scrolling functions
const uint8_t MESG_SIZE = 255;
const uint8_t CHAR_SPACING = 1;

uint16_t SCROLL_DELAY = 100;    // 1 200
uint8_t intensity = 5;         // 1 ... 15

char curMessage[MESG_SIZE];
char newMessage[MESG_SIZE];
bool newMessageAvailable = false;

#define OFF false
#define ON  true

bool matrix_state = true;

/*
char *err2Str(wl_status_t code)
{
  switch (code)
  {
    case WL_IDLE_STATUS:    return ("IDLE");           break; // WiFi is in process of changing between statuses
    case WL_NO_SSID_AVAIL:  return ("NO_SSID_AVAIL");  break; // case configured SSID cannot be reached
    case WL_CONNECTED:      return ("CONNECTED");      break; // successful connection is established
    case WL_CONNECT_FAILED: return ("CONNECT_FAILED"); break; // password is incorrect
    case WL_DISCONNECTED:   return ("CONNECT_FAILED"); break; // module is not configured in station mode
    default: return ("??");
  }
}
*/


void OTA_setup (void)
{
  // Ergänzung OTA
  // Port defaults to 8266
  ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("LED_MATRIX");

  // No authentication by default
  ArduinoOTA.setPassword((const char *)"admin");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();

  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

uint8_t htoi(char c)
{
  c = toupper(c);
  if ((c >= '0') && (c <= '9')) return (c - '0');
  if ((c >= 'A') && (c <= 'F')) return (c - 'A' + 0xa);
  return (0);
}

boolean getText(char *szMesg, char *psz, uint8_t len)
{
  boolean isValid = false;  // text received flag
  char *pStart, *pEnd;      // pointer to start and end of text

  // get pointer to the beginning of the text
  pStart = strstr(szMesg, "/&MSG=");

  if (pStart != NULL)
  {
    pStart += 6;  // skip to start of data
    pEnd = strstr(pStart, "/&");

    if (pEnd != NULL)
    {
      while (pStart != pEnd)
      {
        if ((*pStart == '%') && isdigit(*(pStart + 1)))
        {
          // replace %xx hex code with the ASCII character
          char c = 0;
          pStart++;
          c += (htoi(*pStart++) << 4);
          c += htoi(*pStart++);
          *psz++ = c;
        }
        else
          *psz++ = *pStart++;
      }

      *psz = '\0'; // terminate the string
      isValid = true;
    }
  }

  return (isValid);
}


void scrollDataSink(uint8_t dev, MD_MAX72XX::transformType_t t, uint8_t col)
// Callback function for data that is being scrolled off the display
{
#if PRINT_CALLBACK
  Serial.print("\n cb ");
  Serial.print(dev);
  Serial.print(' ');
  Serial.print(t);
  Serial.print(' ');
  Serial.println(col);
#endif
}

uint8_t scrollDataSource(uint8_t dev, MD_MAX72XX::transformType_t t)
// Callback function for data that is required for scrolling into the display
{
  static enum { S_IDLE, S_NEXT_CHAR, S_SHOW_CHAR, S_SHOW_SPACE } state = S_IDLE;
  static char *p;
  static uint16_t curLen, showLen;
  static uint8_t  cBuf[8];
  uint8_t colData = 0;

  //if (newMessageAvailable)  state = S_IDLE;

  // finite state machine to control what we do on the callback
  switch (state)
  {
    case S_IDLE: // reset the message pointer and check for new message to load
      PRINTS("\nS_IDLE");
      p = curMessage;      // reset the pointer to start of message
      if (newMessageAvailable)  // there is a new message waiting
      {
        strcpy(curMessage, newMessage); // copy it in
        newMessageAvailable = false;
      }
      state = S_NEXT_CHAR;
      break;

    case S_NEXT_CHAR: // Load the next character from the font table
      PRINTS("\nS_NEXT_CHAR");
      if (*p == '\0')
        state = S_IDLE;
      else
      {
        showLen = mx.getChar(*p++, sizeof(cBuf) / sizeof(cBuf[0]), cBuf);
        curLen = 0;
        state = S_SHOW_CHAR;
      }
      break;

    case S_SHOW_CHAR: // display the next part of the character
      PRINTS("\nS_SHOW_CHAR");
      colData = cBuf[curLen++];
      if (curLen < showLen)
        break;

      // set up the inter character spacing
      showLen = (*p != '\0' ? CHAR_SPACING : (MAX_DEVICES * COL_SIZE) / 2);
      curLen = 0;
      state = S_SHOW_SPACE;
    // fall through

    case S_SHOW_SPACE:  // display inter-character spacing (blank column)
      PRINT("\nS_ICSPACE: ", curLen);
      PRINT("/", showLen);
      curLen++;
      if (curLen == showLen)
        state = S_NEXT_CHAR;
      break;

    default:
      state = S_IDLE;
  }

  return (colData);
}

long clearTimer = 0;

void scrollText(void) {
  static uint32_t  prevTime = 0;

  // Is it time to scroll the text?
  if (millis() - prevTime >= SCROLL_DELAY)
  {
    mx.transform(MD_MAX72XX::TSL);  // scroll along - the callback will load all the data
    prevTime = millis();      // starting point for next time
  }
  // nach 2 Min "aus"machen
  /*
    if (millis() - clearTimer >= 60000) {
    //mx.clear();
    newMessageAvailable = true;
    newMessage[0] = '\0';
    }
  */
  if (matrix_state == false) {
    mx.clear();
    newMessageAvailable = true;
    newMessage[0] = '\0';
  }
}




bool intensity_update = false;

void MQTTcallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived @ PUB [");
  Serial.print(topic);
  Serial.print("] ");
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  if (strcmp(topic, sub_value) == 0) {
    newMessageAvailable = true;
    strcpy(newMessage, (char*) payload); // copy it in
    matrix_state = true;
    clearTimer = millis();      // starting point for next time
  }

  if (strcmp(topic, sub_speed) == 0) {
    char* helper_speed = NULL;
    helper_speed = (char*) payload;
    helper_speed[length] = '\n';

    SCROLL_DELAY = atoi (helper_speed);
    if (SCROLL_DELAY > 255) SCROLL_DELAY = 255;
  }

  if (strcmp(topic, sub_intensity) == 0) {
    char* helper_intensity = NULL;
    helper_intensity = (char*) payload;
    helper_intensity[length] = '\n';
    intensity = atoi (helper_intensity);
    if (intensity > 15) intensity = 15;
    intensity_update = true;
  }

  if (strcmp(topic, sub_OnOff) == 0) {
    if ((char)payload[0] == '0')
      matrix_state = false;
    else
      matrix_state = true;
  }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.println("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("LEDMATRIX")) {
      client.publish("outTopic", "LED_MATRIX");
      client.subscribe(sub_value); client.loop();
      client.subscribe(sub_speed); client.loop();
      client.subscribe(sub_intensity); client.loop();
      client.subscribe(sub_OnOff); client.loop();
      Serial.println("connected ...");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


int LdrValue = 0;

void setup(void)
{
  Serial.begin(115200);
#if DEBUG
  Serial.begin(115200);
  PRINTS("\n[MD_MAX72XX WiFi Message Display]\nType a message for the scrolling display from your internet browser");
#endif

#if LED_HEARTBEAT
  pinMode(HB_LED, OUTPUT);
  digitalWrite(HB_LED, LOW);
#endif

  pinMode(D0, OUTPUT);
  digitalWrite(D0, LOW);

  // Display initialization
  mx.begin();
  mx.setShiftDataInCallback(scrollDataSource);
  mx.setShiftDataOutCallback(scrollDataSink);
  mx.control(MD_MAX72XX::INTENSITY, intensity); // INTENSITY 0 ... 15

  curMessage[0] = newMessage[0] = '\0';


  WiFiManager wifiManager;
  //wifiManager.resetSettings();          //mit diesem befehl kannst die gespeicherten werte löschen
  wifiManager.autoConnect("LAUFSCHRIFT");
  Serial.println("verbunden!");

  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  OTA_setup();

  // Connect to and initialize WiFi network
  PRINT("\nConnecting to ", ssid);


  PRINTS("\nWiFi connected");
  char help[255] = "connected!";
  strcpy(curMessage, help);

  client.setServer(mqtt_server, 1883);
  client.setCallback(MQTTcallback);

}


void loop(void) {
  // MQTT connect
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // OTA handler !
  ArduinoOTA.handle();
/*
  static uint32_t timeLDRLast = 0;

  if (millis() - timeLDRLast >= 1000)
  {
    timeLDRLast = millis();
    digitalWrite(D0, LOW);
    LdrValue = analogRead(A0);
    LdrValue /= 64;

    Serial.println(LdrValue);

    if (intensity <= LdrValue)
      mx.control(MD_MAX72XX::INTENSITY, 1);
    else
      mx.control(MD_MAX72XX::INTENSITY, intensity - LdrValue);
  }
*/

  if (intensity_update) {
    //if (intensity <= LdrValue)
    //  mx.control(MD_MAX72XX::INTENSITY, 1);
    //else
    //  mx.control(MD_MAX72XX::INTENSITY, intensity - LdrValue);
    mx.control(MD_MAX72XX::INTENSITY, intensity);
  }

#if LED_HEARTBEAT
  static uint32_t timeLast = 0;

  if (millis() - timeLast >= HB_LED_TIME)
  {
    digitalWrite(HB_LED, digitalRead(HB_LED) == LOW ? HIGH : LOW);
    timeLast = millis();
  }
#endif
  scrollText();
}
