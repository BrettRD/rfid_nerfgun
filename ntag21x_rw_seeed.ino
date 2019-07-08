
/*
players are issued ten darts each,
darts are coded to the gun
if the wrong dart is fired, the gun jams
*/

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
const int ss_pin = D0;
const int mot_a = D2;
const int mot_b = D1;
const int interlock_pin = A0;

const float rail_filter_alpha = 0.1;
const int rail_voltage_min = 300;   //minimum plausible raw adc read from the rail
float rail_voltage_filtered = 0;


uint32_t last_loop_timestamp = 0;
uint32_t timestamp = 0;

uint32_t filter_timestamp = 0;

uint32_t report_period = 1000;
uint32_t report_time = 0;


bool mode_register_darts = false;
bool motor_jam = false;   //game mechanic, brake the flywheel
int motor_power_max = 700;
int motor_power = 0;


const int max_darts = 30;
int n_darts = 0;
uint8_t dart_list[max_darts][7];



//commands:
//erase all darts
//add this dart
//



ESP8266WiFiMulti wifiMulti;
MQTTClient mqtt;


PN532_SPI intf(SPI, ss_pin);
PN532 nfc = PN532(intf);

//uint8_t password[4] =  {0x12, 0x34, 0x56, 0x78};
//uint8_t buf[4];
uint8_t uid[7];         //store the tag uid
char uid_hex[20];       //format to hex (only 2*7 + 1 bytes used)
uint8_t uidLength = 7;

// change this during setup, examples here
char client_id[30] = "testgun";
char pubTopic[60] = "nerfgun/testgun/lwt";
char tagTopic[60] = "nerfgun/testgun/tag";
char stateTopic[60] = "nerfgun/testgun/state";
char subTopic[60] = "nerfgun/testgun/cmd";



char mqtt_host_string[50];
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
    mode_register_darts = false;
    //Serial.printf("MQTT lost\n");
    mqttConnect();

}


void mqtt_cb_onData(String topic, String data, bool cont)
{
    if(topic == subTopic)
    {
        int power = data.toInt();
        if(power<0) power = 0;
        analogWrite(mot_a, power);
        digitalWrite(mot_b, LOW);
    }
    //parse a json command
    //n_darts = 0   clears the list, is set by command
    //mode_register_darts is set by command
}


void mqttConnect()
{
    mqtt.begin(mqtt_host_string, {.lwtTopic = pubTopic, .lwtMsg = "{\"online\":false}", .lwtQos = 0, .lwtRetain = 0});
}


//returns dst
char* to_hex(char* dst, uint8_t* src, int n)
{
    for(uint8_t i=0; i<n; i++)
    {
        dst[(2*i)+0] = "0123456789abcdef"[src[i] >>  4 ];
        dst[(2*i)+1] = "0123456789abcdef"[src[i] & 0x0f];
    }
    dst[2*n] = '\0';
    return dst;
}


bool register_dart(uint8_t* uid)
{
    if(tag_match(uid))
    {
        return true;
    }
    else
    {
        if(n_darts < max_darts)
        {
            memcpy(dart_list[n_darts], uid, 7);
            n_darts++;
            return true;
        }
    }
    return false;
}


bool tag_match(uint8_t* tag)
{
    for(int i=0; i<n_darts; i++)
    {
        if(0 == memcmp(dart_list[i], tag, 7))
        {
            return true;
        }
    }
    return false;
}



void run_motor(int power, bool jam, bool interlock)
{
    //if the spin button is released, clear the software jam and power down
    //otherwise, spin up to the target speed
    if(!interlock)
    {
        jam = false;
        motor_power = 0;
    }
    else
    {
        motor_power = power;
    }


    //apply power to the motor
    //hit the brakes if the jam flag is set
    if(jam == false)
    {
        analogWrite(mot_a, motor_power);
        digitalWrite(mot_b, LOW);
    }
    else
    {
        digitalWrite(mot_a, HIGH);  //brake
        digitalWrite(mot_b, HIGH);
    }
}



void setup(void)
{
    //choose the names to use
    sprintf(client_id, "nerf%06X", ESP.getChipId());
    sprintf(pubTopic, "nerfgun/%s/lwt", client_id);
    sprintf(subTopic, "nerfgun/%s/cmd", client_id);
    sprintf(tagTopic, "nerfgun/%s/tag", client_id);
    sprintf(stateTopic, "nerfgun/%s/state", client_id);
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
    last_loop_timestamp = timestamp;
    timestamp = millis();


    bool wifi_online = (wifiMulti.run() == WL_CONNECTED);

    int rail_voltage = analogRead(interlock_pin);
    bool interlock = rail_voltage > rail_voltage_min;
    if(interlock)
    {
        float rail_filter_alpha_t = rail_filter_alpha * (float)(timestamp - filter_timestamp) / 1000.0;
        rail_voltage_filtered = (((float)rail_voltage * rail_filter_alpha) + rail_voltage_filtered) / (1 + rail_filter_alpha);
    }

    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength))
    {
        if(uidLength == 7)
        {
            to_hex(uid_hex, uid, 7);
            if(wifi_online)
            {
                sprintf(jsonString, "{\"uid\":\"%s\"}", uid_hex);
                mqtt.publish(tagTopic, jsonString, 0, 0);
            }

            if(mode_register_darts)
            {
                register_dart(uid);
            }
            else
            {
                motor_jam = !tag_match(uid);
            }
/*
            Serial.print("uid = ");
            Serial.println(uid_hex);
*/
        }
    }


    run_motor(motor_power_max, motor_jam, interlock);

    if(timestamp > report_time + report_period)
    {
        report_time += report_period;
        if(wifi_online)
        {
            sprintf(jsonString, "{\"interlock\":%d, \"voltage\":%f}", interlock, rail_voltage_filtered);
            mqtt.publish(stateTopic, jsonString, 0, 0);
        }
    }

    mqtt.handle();

    //Serial.print("MQTT handled: ");
    //Serial.println(millis());

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
