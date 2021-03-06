/**
 * This node is supposed to be connected via an W5100 ethernet board.
 * 
 * Outputs :
 *   - Light in the garage "overheadlight"
 *   - Boiler parents "boilerparents"
 *   - Boiler Aymeric "boileraymeric"
 * Inputs :
 *   - Current sensor on garage door "garagedoormovement"
 *   - Switch to control lights "lightswitch"
 */

#include <SPI.h>
#include "Ethernet.h"
#include <PubSubClient.h>

#define PIN_LIGHT_RELAY 4
#define PIN_BOILER_PARENTS_RELAY 5
#define PIN_BOILER_AYMERIC_RELAY 6
#define RELAY_ON 1  // GPIO value to write to turn on attached relay
#define RELAY_OFF 0 // GPIO value to write to turn off attached relay
#define PIN_SWITCH_INPUT 2 // Pin used to detect the switch state
#define PIN_CURRENT_SENSOR 14
#define CURRENT_SENSOR_THRES 8
#define CURRENT_SENSOR_SMOOTHING_NBR_READINGS 1000
#define SWITCH_CHANGE_DEBOUNCE_MILLIS 300   //Original 275

static bool backupLightManagement = true; //Will always toggle the light when the switch is pressed, whatever MQTT says

byte mac[] = {0xDE, 0xAA, 0xAA, 0x01, 0x00, 0x00};
IPAddress ip = (192, 168, 1, 50);
IPAddress MQTTserver(192, 168, 1, 150);

bool lastSwitchState;
unsigned long lastSwitchChange=0;
EthernetClient ethClient;
PubSubClient client(ethClient);
unsigned long MQTTLastReconnectAttempt = 0;
unsigned long actualMesure = 0;
unsigned int nbrMesures = 0;

void setup() {
  pinMode(PIN_LIGHT_RELAY, OUTPUT);
  pinMode(PIN_BOILER_PARENTS_RELAY, OUTPUT);
  pinMode(PIN_BOILER_AYMERIC_RELAY, OUTPUT);
  pinMode(PIN_SWITCH_INPUT, INPUT_PULLUP);
  pinMode(PIN_CURRENT_SENSOR, INPUT);
  
  setRelay(PIN_LIGHT_RELAY, false);
  setRelay(PIN_BOILER_PARENTS_RELAY, true);
  setRelay(PIN_BOILER_AYMERIC_RELAY, true);
  lastSwitchState = digitalRead(PIN_SWITCH_INPUT);

  Serial.begin(115200);

  // start the Ethernet connection:
  if (Ethernet.begin(mac) == 0) {
    Serial.println("Failed to configure Ethernet using DHCP. Falling back to static IP.");
    // try to congifure using IP address instead of DHCP:
    Ethernet.begin(mac, ip);
  }

  //Setting MQTT up
  client.setServer(MQTTserver, 1883);
  client.setCallback(MQTTMessageReceived);
  
  // give the Ethernet shield a second to initialize:
  delay(1000);
}

void loop() {
  manageMQTTConnexion();
  manageSwitchToggle();
  manageCurrentSensor();
}

void setRelay(byte pin, bool state) {
  if (pin == PIN_LIGHT_RELAY) {
    digitalWrite(PIN_LIGHT_RELAY, state ? RELAY_ON : RELAY_OFF);
    client.publish("/jarvis/out/state/rez/garage/OverheadLight", getTruthValueFromBool(state));
  }
  else if (pin == PIN_BOILER_PARENTS_RELAY) {
    digitalWrite(PIN_BOILER_PARENTS_RELAY, state ? RELAY_ON : RELAY_OFF);
    client.publish("/jarvis/out/state/rez/garage/BoilerParents", getTruthValueFromBool(state));
  }
  else if (pin == PIN_BOILER_AYMERIC_RELAY) {
    digitalWrite(PIN_BOILER_AYMERIC_RELAY, state ? RELAY_ON : RELAY_OFF);
    client.publish("/jarvis/out/state/rez/garage/BoilerAymeric", getTruthValueFromBool(state));
  }
}

const char* getTruthValueFromBool (bool input) {
  return input ? "on" : "off";
}

void manageCurrentSensor() {
  if (currentSensorTriggered()) {
    client.publish("/jarvis/out/state/rez/garage/GarageDoorMovement","on");
  }
}

bool currentSensorTriggered() {
  if (nbrMesures > CURRENT_SENSOR_SMOOTHING_NBR_READINGS) {
    int val = actualMesure/nbrMesures;
    actualMesure = 0;
    nbrMesures = 0;
    Serial.println(val);
    return val > CURRENT_SENSOR_THRES;
  }
  else {
    actualMesure += abs((int)analogRead(PIN_CURRENT_SENSOR) - (int)511);
    nbrMesures++;
    return false;
  }
}

bool hasSwitchChanged() {
  bool currentSwitchState = digitalRead(PIN_SWITCH_INPUT);
  if (millis() - lastSwitchChange > SWITCH_CHANGE_DEBOUNCE_MILLIS &&
      lastSwitchState != currentSwitchState) {
    lastSwitchState = currentSwitchState;
    lastSwitchChange = millis();
    return true;
  }
  return false;
}

void manageSwitchToggle() {
  if (hasSwitchChanged()) {
    client.publish("/jarvis/out/state/rez/garage/LightSwitch","toggle");
    if (backupLightManagement)
      toggleLight();
  }
}

void toggleLight() {
  if (digitalRead(PIN_LIGHT_RELAY) == RELAY_ON) {
    setRelay(PIN_LIGHT_RELAY, false);
  }
  else {
    setRelay(PIN_LIGHT_RELAY, true);
  }
}

void MQTTMessageReceived(char* topic, byte* payload, unsigned int length) {
  if(strcmp(topic, "/jarvis/in/command/rez/garage/OverheadLight") == 0) {
    if (strcmp(payload, "toggle") == 0)
      toggleLight();
    else
      setRelay(PIN_LIGHT_RELAY, getTruthValue(payload));
  }
  else if(strcmp(topic, "/jarvis/in/command/rez/garage/BoilerParents") == 0)
    setRelay(PIN_BOILER_PARENTS_RELAY, getTruthValue(payload));
  else if(strcmp(topic, "/jarvis/in/command/rez/garage/BoilerAymeric") == 0)
    setRelay(PIN_BOILER_AYMERIC_RELAY, getTruthValue(payload));
}

//If unclear, defaults to false
bool getTruthValue(const char* string) {
  if (strcmp(string, "ON") == 0)
    return true;
  else if (strcmp(string, "On") == 0)
    return true;
  else if (strcmp(string, "on") == 0)
    return true;
  else if (strcmp(string, "1") == 0)
    return true;
  else if (strcmp(string, "TRUE") == 0)
    return true;
  else if (strcmp(string, "True") == 0)
    return true;
  else if (strcmp(string, "true") == 0)
    return true;
  else
    return false;
}

void manageMQTTConnexion() {
  if (!client.connected()) {
    long now = millis();
    if (now - MQTTLastReconnectAttempt > 5000) {
      MQTTLastReconnectAttempt = now;
      // Attempt to reconnect
      if (reconnect()) {
        MQTTLastReconnectAttempt = 0;
      }
    }
  } else {
    // We are still connected
    client.loop();
  }
}

boolean reconnect() {
  if (client.connect("rez-Garage")) {
    // Once connected, publish an announcement
    client.publish("/jarvis/out/state/rez/garage","New connection");
    // ... and (re)subscribe
    client.subscribe("/jarvis/in/+/rez/garage/#");
  }
  return client.connected();
}

