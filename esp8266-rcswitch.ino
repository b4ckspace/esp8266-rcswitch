#include "SparkFunHTU21D.h"
#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <SimpleTimer.h>
#include <RCSwitch.h>h
#include "settings.h"
#include <queue>

uint8_t mqttBaseTopicSegmentCount = 0;
char convertBuffer[10] = {0};
bool isSending = false;
unsigned long lastAlienSignalMs = 0;

typedef struct {
  char systemCode[6] = {0};
  char unitCode[6] = {0};
  bool on;
} rcJob;

std::queue<rcJob> rcJobQueue;

unsigned long nextJobMillis = 0;

WiFiClient wifiClient;
PubSubClient mqttClient;

SimpleTimer timer;
HTU21D htu21;
RCSwitch rcSwitch = RCSwitch();

void onAlienSignal() {
  if (isSending) {
    return;
  }

  lastAlienSignalMs = millis();
}

void mqttConnect() {
  while (!mqttClient.connected()) {
    if (mqttClient.connect(HOSTNAME, MQTT_TOPIC_STATE, 1, true, "disconnected")) {
      mqttClient.subscribe(MQTT_TOPIC_RCSWITCH);
      mqttClient.subscribe(MQTT_TOPIC_MQTTESP);
      mqttClient.publish(MQTT_TOPIC_STATE, "connected", true);
    } else {
      Serial.println("MQTT connect failed!");
      delay(1000);
    }
  }
}

bool isCodeValid(char* code) {

  if (strlen(code) != 5) {
    return false;
  }

  for (uint8_t i = 0; i < 5; i++) {
    if (code[i] != '0' && code[i] != '1') {
      return false;
    }
  }

  return true;
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  
  uint8_t segment = 0;
  char *token;
  rcJob job;

  if (strncmp(topic, MQTT_TOPIC_MQTTESP, strlen(MQTT_TOPIC_MQTTESP)) == 0) {
    
    // MQTT Connection TEST - LED Indicator
    if (strncmp((char*) payload, "ON", length) == 0) {
      digitalWrite(BUILTIN_LED, LOW);
    } else if (strncmp((char*) payload, "OFF", length) == 0) {
      digitalWrite(BUILTIN_LED, HIGH);
    }

    return;
  }
    
  // RC-Switch
  token = strtok((char*) topic, MQTT_TOPIC_DELIMITER);

  while (token != NULL) {
    if (segment == mqttBaseTopicSegmentCount) {
        strncpy(job.systemCode, token, 5);
    } else if (segment == mqttBaseTopicSegmentCount + 1) {
        strncpy(job.unitCode, token, 5);
    }

    // Bounds checking...
    if (segment > mqttBaseTopicSegmentCount + 1) {
      return;
    }

    token = strtok(NULL, MQTT_TOPIC_DELIMITER);
    segment++;
  }

  // Check for powersocket-commands
  if (isCodeValid(job.systemCode) && isCodeValid(job.unitCode)) {
    
    if (strncmp((char*) payload, "ON", length) == 0) {
      job.on = true;
    } else if (strncmp((char*) payload, "OFF", length) == 0) {
      job.on = false;
    } else {
      return;
    }
  
    rcJobQueue.push(job);
  }
  
}

void setup() {
  pinMode(BUILTIN_LED, OUTPUT);
  Serial.begin(115200);

  pinMode(INTERRUPT_PIN, INPUT);
  attachInterrupt(INTERRUPT_PIN, onAlienSignal, CHANGE);

  rcSwitch.enableTransmit(D6);
  rcSwitch.setRepeatTransmit(RCSWITCH_TRANSMISSIONS);
  
  htu21.begin();

  // Count mqtt "segments" of base topic
  for (uint8_t i = 0; i < strlen(MQTT_TOPIC_RCSWITCH); i++) {
     if (MQTT_TOPIC_RCSWITCH[i] == MQTT_TOPIC_DELIMITER[0]) {
        mqttBaseTopicSegmentCount++;
     }
  }
  
  WiFi.hostname(HOSTNAME);
  WiFi.mode(WIFI_STA);
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  
  
  mqttClient.setClient(wifiClient);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.begin();

  timer.setInterval(SENSOR_REFRESH_MS, []() {
    dtostrf(htu21.readHumidity(), 4, 2, convertBuffer);
    mqttClient.publish(MQTT_TOPIC_HUMIDITY, convertBuffer);

    dtostrf(htu21.readTemperature(), 4, 2, convertBuffer);
    mqttClient.publish(MQTT_TOPIC_TEMPERATURE, convertBuffer);
  });

  nextJobMillis = millis();
}
void loop() {
  mqttConnect();

  if (!rcJobQueue.empty() && millis() > nextJobMillis) {

    // Wait because there are alien signals!
    while(lastAlienSignalMs + ALIEN_SIGNAL_BACKOFF_MS < millis()) {
      delay(2);
    }


    rcJob job = rcJobQueue.front();
    rcJobQueue.pop();

    noInterrupts();
    digitalWrite(BUILTIN_LED, LOW);

    isSending = true;
    
    if (job.on) {
      rcSwitch.switchOn(job.systemCode, job.unitCode);
    } else {
      rcSwitch.switchOff(job.systemCode, job.unitCode);
    }
    
    isSending = false;

    digitalWrite(BUILTIN_LED, HIGH);
    interrupts(); 
    
    nextJobMillis = millis() + RCSWITCH_PAUSE_MS;
  }

  timer.run();
  mqttClient.loop();
  ArduinoOTA.handle();
}
