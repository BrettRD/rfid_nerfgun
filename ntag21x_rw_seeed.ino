

#include <SPI.h>
#include <PN532_SPI.h>
#include <PN532.h>
#include <NfcAdapter.h>


#include <ESP8266MQTTClient.h>
#include <ESP8266WiFi.h>

#include "secrets.h"
/* defined in secrets.h
char* ap_name = "";
char* ap_pass = "";
char* broker_url = ""
*/
char mqtt_host_string[50];

MQTTClient mqtt;

int ss_pin = D0;
int mot_a = D2;
int mot_b = D1;
int interlock_pin = A0;

int rail_voltage;
bool interlock  = false;


PN532_SPI intf(SPI, ss_pin);
PN532 nfc = PN532(intf);

//uint8_t password[4] =  {0x12, 0x34, 0x56, 0x78};
//uint8_t buf[4];
uint8_t uid[7];         //store the tag uid
char uid_hex[20];       //format to hex
uint8_t uidLength;

// change this during setup, examples here
char client_id[20] = "nerfgun";
char pubTopic[20] = "nerfgun/lwt";
char tagTopic[20] = "nerfgun/tag";

char jsonString[100]; //to store the formatted json

unsigned long last_wifi_reconnect_attempt = 0;


void wifiConnect()
{
  WiFi.hostname("esp8266");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ap_name, ap_pass);

  while(WiFi.status() != WL_CONNECTED)
  {
      delay(100);
      Serial.print(".");
  }
  Serial.printf("Connected\n");

}

void cb_onSubscribe(int sub_id)
{
}

void cb_onConnect()
{
  Serial.printf("MQTT reconnected\n");
  mqtt.publish(pubTopic, "{\"online\":true}", 0, 0);
  mqtt.subscribe("power", 0);

}

void cb_onDisconnect()
{
  checkWifi();
  Serial.printf("MQTT lost\n");
  delay(10);
  mqttConnect();
}

void cb_onData(String topic, String data, bool cont)
{
    Serial.println(topic);
    Serial.println(data);
    
    if(topic == "power")
    {
        int power = data.toInt();
        //if(power>255) power=255;
        if(power<0) power = 0;
        analogWrite(mot_a, power);
        digitalWrite(mot_b, LOW);
    }
}



void mqttConnect()
{
    mqtt.begin(mqtt_host_string, {.lwtTopic = pubTopic, .lwtMsg = "{\"online\":false}", .lwtQos = 0, .lwtRetain = 0});
}


void checkWifi()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        unsigned long now = millis();
        if (now - last_wifi_reconnect_attempt > 20000UL)
        {
            Serial.println("Attempting to connect to WiFi");
            last_wifi_reconnect_attempt = now;
            wifiConnect();
        }
        return;
    }
}


void publishTag(uint8_t* uid, bool interlock, int rail_voltage)
{
    for(uint8_t i=0; i<7; i++)
    {
      uid_hex[(2*i)+0] = "0123456789abcdef"[uid[i] >>  4 ];
      uid_hex[(2*i)+1] = "0123456789abcdef"[uid[i] & 0x0f];
    }
    uid_hex[16] = '\0'; //null terminate

    if(interlock)
    {
        sprintf(jsonString, "{\"uid\":\"%s\", \"interlock\":%d, \"voltage\":%d}", uid_hex, interlock, rail_voltage);
    }
    else
    {
        sprintf(jsonString, "{\"uid\":\"%s\", \"interlock\":%d}", uid_hex, interlock);
    }
    mqtt.publish(tagTopic, jsonString, 0, 0);
}



void setup(void)
{
    //choose the names to use
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

    wifiConnect();
    last_wifi_reconnect_attempt = millis();

    //mqtt.onSecure(cb_onSecure);
    mqtt.onData(cb_onData);
    mqtt.onSubscribe(cb_onSubscribe);
    mqtt.onConnect(cb_onConnect);
    mqtt.onDisconnect(cb_onDisconnect);
    Serial.printf("Callbacks Set\n");
    delay(10);

    mqttConnect();


}

void loop(void)
{
    checkWifi();

    //Serial.println("wait for a tag");
    // wait until a tag is present
    //while (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength)){}
    rail_voltage = analogRead(interlock_pin);
    interlock = rail_voltage > 300;

    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength))
    {
        if(uidLength == 7)
        {
            if(rail_voltage > 300)
            {
                //analogWrite(mot_a, 50);
                //digitalWrite(mot_b, LOW);
            }
            publishTag(uid, interlock, rail_voltage);

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
