


#include <SPI.h>
#include <PN532_SPI.h>
#include <PN532.h>
#include <NfcAdapter.h>


#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>

#include <ESP8266MQTTClient.h>

#include "secrets.h"
/* defined in secrets.h
char* ap_name = "";
char* ap_pass = "";
char* broker_url = ""
*/
char mqtt_host_string[50];


ESP8266WiFiMulti wifiMulti;
MQTTClient mqtt;

const int ss_pin = D0;
const int mot_a = D2;
const int mot_b = D1;
const int interlock_pin = A0;

const float rail_filter_alpha = 0.1;
const int rail_voltage_min = 300;   //minimum plausible raw adc read from the rail

float rail_voltage_filtered = 0;


PN532_SPI intf(SPI, ss_pin);
PN532 nfc = PN532(intf);

//uint8_t password[4] =  {0x12, 0x34, 0x56, 0x78};
//uint8_t buf[4];
uint8_t uid[7];         //store the tag uid
char uid_hex[20];       //format to hex (only 2*7 + 1 bytes used)
uint8_t uidLength;

// change this during setup, examples here
char client_id[30] = "testgun";
char pubTopic[60] = "nerfgun/testgun/lwt";
char tagTopic[60] = "nerfgun/testgun/state";
char subTopic[60] = "nerfgun/testgun/power";

char jsonString[100]; //to store the formatted json


void mqtt_cb_onSubscribe(int sub_id)
{
}


void mqtt_cb_onConnect()
{
    Serial.printf("MQTT reconnected\n");
    mqtt.publish("nerfgun/name", client_id, 0, 0);
    mqtt.publish(pubTopic, "{\"online\":true}", 0, 0);
    mqtt.subscribe(subTopic, 0);
}


void mqtt_cb_onDisconnect()
{
  //Serial.printf("MQTT lost\n");
  mqttConnect();
}


void mqtt_cb_onData(String topic, String data, bool cont)
{
    Serial.println(topic);
    Serial.println(data);
    
    if(topic == subTopic)
    {
        int power = data.toInt();
        if(power<0) power = 0;
        analogWrite(mot_a, power);
        digitalWrite(mot_b, LOW);
    }
}


void mqttConnect()
{
    mqtt.begin(mqtt_host_string, {.lwtTopic = pubTopic, .lwtMsg = "{\"online\":false}", .lwtQos = 0, .lwtRetain = 0});
}


void publishTag(uint8_t* uid, bool interlock, float rail_voltage)
{
    for(uint8_t i=0; i<7; i++)
    {
      uid_hex[(2*i)+0] = "0123456789abcdef"[uid[i] >>  4 ];
      uid_hex[(2*i)+1] = "0123456789abcdef"[uid[i] & 0x0f];
    }
    uid_hex[14] = '\0'; //null terminate

    sprintf(jsonString, "{\"uid\":\"%s\", \"interlock\":%d, \"voltage\":%f}", uid_hex, interlock, rail_voltage);

    mqtt.publish(tagTopic, jsonString, 0, 0);
}


void setup(void)
{
    //choose the names to use
    sprintf(client_id, "nerf%06X", ESP.getChipId());
    sprintf(pubTopic,"nerfgun/%s/lwt", client_id);
    sprintf(subTopic,"nerfgun/%s/power", client_id);
    sprintf(tagTopic, "nerfgun/%s/state", client_id);
    sprintf(mqtt_host_string, "mqtt://%s:%s#$s", broker_url, "1883", client_id);

    pinMode(mot_a, OUTPUT);
    pinMode(mot_b, OUTPUT);
    digitalWrite(mot_a, LOW);
    digitalWrite(mot_b, LOW);

    pinMode(interlock_pin, INPUT);
    pinMode(ss_pin, OUTPUT);

    SPI.begin();
    Serial.begin(115200);
    Serial.println("NTAG21x R/W");

    nfc.begin();
    nfc.SAMConfig();

    WiFi.hostname(client_id);
    WiFi.mode(WIFI_STA);
    wifiMulti.addAP(ap_name, ap_pass);

    //mqtt.onSecure(mqtt_cb_onSecure);
    mqtt.onData(mqtt_cb_onData);
    mqtt.onSubscribe(mqtt_cb_onSubscribe);
    mqtt.onConnect(mqtt_cb_onConnect);
    mqtt.onDisconnect(mqtt_cb_onDisconnect);
    Serial.printf("Callbacks Set\n");

    mqttConnect();
}

void loop(void)
{
    bool wifi_online = (wifiMulti.run() == WL_CONNECTED);

    int rail_voltage = analogRead(interlock_pin);
    bool interlock = rail_voltage > rail_voltage_min;
    if(interlock)
    {
        rail_voltage_filtered = (((float)rail_voltage * rail_filter_alpha) + rail_voltage_filtered) / (1 + rail_filter_alpha);
    }

    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength))
    {
        if(uidLength == 7)
        {

            if(wifi_online)
            {
                publishTag(uid, interlock, rail_voltage);
            }

            Serial.print("uid = ");
            for(uint8_t i=0; i<uidLength; i++)
            {
                Serial.print("0123456789abcdef"[uid[i] >> 4]);
                Serial.print("0123456789abcdef"[(uid[i] & 0x0f)]);
            }
            Serial.println("");
        }
    }

    mqtt.handle();


    // if NTAG21x enables r/w protection, uncomment the following line 
    // nfc.ntag21x_auth(password);
/*
    nfc.mifareultralight_ReadPage(3, buf);
    int capacity = buf[2] * 8;
    Serial.print(F("Tag capacity "));
    Serial.print(capacity);
    Serial.println(F(" bytes"));

    for (int i=4; i<capacity/4; i++) {
        nfc.mifareultralight_ReadPage(i, buf);
        nfc.PrintHexChar(buf, 4);
    }

    // wait until the tag is removed
    while (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength)) {
    }
*/
}
