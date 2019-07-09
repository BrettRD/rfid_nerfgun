
/*
players are issued ten darts each,
darts are coded to the gun
if the wrong dart is fired, the gun jams
*/

#include <SPI.h>
#include <PN532_SPI.h>
#include <PN532.h>
#include <NfcAdapter.h>

#include <ArduinoJson.h>

#include <ESP8266WiFi.h>

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
const int tag_len = 7;

const float rail_voltage_max = 1024 / ((6.0/2)/3.3);    //10b adc, 1:2 divider, 3v3 reference, 6V max operating condition
const float rail_filter_alpha = 0.1;
const int rail_voltage_min = 300;   //minimum plausible raw adc read from the rail
float rail_voltage_filtered = 0;


uint32_t last_loop_timestamp = 0;
uint32_t timestamp = 0;

uint32_t filter_timestamp = 0;

uint32_t report_period = 2000;
uint32_t report_time = 0;


bool mode_register_darts = false;
const float motor_play_speed = 0.7;
const float motor_register_speed = 0.5;
const int motor_power_max = 1023;
bool motor_jam = false;   //game mechanic, brake the flywheel
float motor_power_run = 0.7;  //70% is comfortable, 50% feels slow, 30% can fail to fire

const int max_darts = 30;
int n_darts = 0;
uint8_t dart_list[max_darts][tag_len];


MQTTClient mqtt;
bool mqtt_connected = false;  //the library does not expose mqtt.connected()


PN532_SPI intf(SPI, ss_pin);
PN532 nfc = PN532(intf);

//uint8_t password[4] =  {0x12, 0x34, 0x56, 0x78};
//uint8_t buf[4];

uint8_t uid[tag_len] = {0,0,0,0,0,0,0};         //store the tag uid
char uid_hex[(2*tag_len)+1];
uint8_t uidLength = tag_len;
uint8_t last_uid[tag_len] = {0,0,0,0,0,0,0};  //prevent mqtt from overloading


// change this during setup, examples here
char client_id[30] = "testgun";
char pubTopic[60] = "nerfgun/testgun/lwt";
char tagTopic[60] = "nerfgun/testgun/tag";
char stateTopic[60] = "nerfgun/testgun/state";
char subTopic[60] = "nerfgun/testgun/cmd";



char mqtt_host_string[50];
char jsonString[100]; //to store the formatted json

unsigned long last_wifi_reconnect_attempt = 0;


void wifiConnect()
{
    WiFi.hostname(client_id);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ap_name, ap_pass);
}


bool checkWifi()
{
    return WiFi.status() == WL_CONNECTED;
}



void mqtt_cb_onSubscribe(int sub_id)
{
}


void mqtt_cb_onConnect()
{
    mqtt_connected = true;
    Serial.printf("MQTT reconnected\n");
    mqtt.publish("nerfgun/name", client_id, 0, 0);
    mqtt.publish(pubTopic, "{\"online\":true}", 0, 0);
    mqtt.subscribe(subTopic, 0);
}


void mqtt_cb_onDisconnect()
{
    mqtt_connected = false;
    exit_register_mode();
    Serial.printf("MQTT lost\n");
    mqttConnect();

}


void mqtt_cb_onData(String topic, String data, bool cont)
{
    if(topic == subTopic)
    {
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, data);
        if (!error) {
            if(!doc["power_override"].isNull())
            {
                int power = doc["power_override"];
                //apply the value and run the motor
            }

            if(!doc["clear_darts"].isNull())    //true clears the list
            {
                if((bool)doc["clear_darts"])
                {
                    n_darts = 0;    //clear the list
                }
            }

            if(!doc["register_mode"].isNull())  //true or false to enter or exit
            {
                mode_register_darts = doc["register_mode"]; //enter or exit register mode
            }

            if(!doc["register_dart"].isNull())  //send the hex string of the dart uid
            {
                if(strlen(doc["register_dart"]) >= 2*tag_len)
                {
                    uint8_t tmp_uid[tag_len];
                    register_dart(from_hex(tmp_uid, doc["register_dart"], tag_len));
                }
            }
        }
    }
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
        dst[(2*i)+0] = "0123456789abcdef"[src[(n-1)-i] >>  4];
        dst[(2*i)+1] = "0123456789abcdef"[src[(n-1)-i] & 0xf];
    }
    dst[2*n] = '\0';
    return dst;
}

uint8_t* from_hex(uint8_t* dst, const char* src, int n)
{
    for(uint8_t i=0; i<n; i++)
    {
        char src_copy[3];
        src_copy[2] = '\0';
        memcpy(src_copy, &src[2*i], 2);
        dst[(n-1)-i] = strtol(src_copy, 0, 16);
    }
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
            memcpy(dart_list[n_darts], uid, tag_len);
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
        if(0 == memcmp(dart_list[i], tag, tag_len))
        {
            return true;
        }
    }
    return false;
}



void run_motor(float power, bool jam, bool interlock)
{
    int motor_power = 0;

    //if the spin button is released, clear the software jam and power down
    //otherwise, spin up to the target speed
    if(!interlock)
    {
        jam = false;
        motor_power = 0;
    }
    else
    {
        float battery_compensation = (rail_voltage_max / rail_voltage_filtered);
        motor_power = motor_power_max * power * battery_compensation;
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

void enter_register_mode()
{
    mode_register_darts = true;
    motor_power_run = motor_register_speed;
}
void exit_register_mode()
{
    mode_register_darts = false;
    motor_power_run = motor_play_speed;
}


void setup(void)
{
    //choose the names to use
    sprintf(client_id, "nerf%06X", ESP.getChipId());
    sprintf(pubTopic, "nerfgun/%s/lwt", client_id);
    sprintf(subTopic, "nerfgun/%s/cmd", client_id);
    sprintf(tagTopic, "nerfgun/%s/tag", client_id);
    sprintf(stateTopic, "nerfgun/%s/state", client_id);
    sprintf(mqtt_host_string, "mqtt://%s:%s#%s", broker_url, "1883", client_id);

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

//    WiFi.hostname(client_id);
//    WiFi.mode(WIFI_STA);
//    wifiMulti.addAP(ap_name, ap_pass);
    wifiConnect();
    last_wifi_reconnect_attempt = millis();



    //mqtt.onSecure(mqtt_cb_onSecure);
    mqtt.onData(mqtt_cb_onData);
    mqtt.onSubscribe(mqtt_cb_onSubscribe);
    mqtt.onConnect(mqtt_cb_onConnect);
    mqtt.onDisconnect(mqtt_cb_onDisconnect);
    Serial.printf("Callbacks Set\n");

    mqttConnect();
    mqtt.handle();
    mqtt.handle();

}




void loop(void)
{

    last_loop_timestamp = timestamp;
    timestamp = millis();

    Serial.print("loop start: ");
    Serial.println(millis());

    bool wifi_online = checkWifi();

    Serial.print("wifi checked: ");
    Serial.print(wifi_online?"online ":"offline ");

    Serial.println(millis());


    int rail_voltage = analogRead(interlock_pin);
    bool interlock = rail_voltage > rail_voltage_min;
    if(interlock)
    {
        float rail_filter_alpha_t = rail_filter_alpha * (float)(timestamp - filter_timestamp) / 1000.0;
        rail_voltage_filtered = (((float)rail_voltage * rail_filter_alpha) + rail_voltage_filtered) / (1 + rail_filter_alpha);
    }

    Serial.print("adc read: ");
    Serial.println(millis());



    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 100, false))
    {
        if(uidLength == tag_len)
        {
            to_hex(uid_hex, uid, tag_len);
            if(wifi_online && mqtt_connected)
            {
                if(0 != memcmp(last_uid, uid, tag_len)) //publish once
                {
                    sprintf(jsonString, "{\"uid\":\"%s\"}", uid_hex);
                    mqtt.publish(tagTopic, jsonString, 0, 0);
                    memcpy(last_uid, uid, tag_len);
                    Serial.println("send dart");

                }
            }

            if(mode_register_darts)
            {
                register_dart(uid);
            }
            else
            {
                motor_jam = !tag_match(uid);
            }

            Serial.print("uid = ");
            Serial.println(uid_hex);

        }
    }


    Serial.print("nfc read: ");
    Serial.println(millis());

    run_motor(motor_power_run, motor_jam, interlock);


    mqtt.handle();  //this will reconnect when the TCP socket comes up

    Serial.print("MQTT handled: ");
    Serial.print(mqtt_connected ? "online " : "offline ");
    Serial.println(millis());


    if(mqtt_connected)
    {
        if(timestamp - report_time > report_period)
        {
            report_time = timestamp;
            sprintf(jsonString, "{\"interlock\":%d, \"voltage\":%f, \"n_darts\":%d, \"jam\":%d, \"register\":%d}", interlock, rail_voltage_filtered, n_darts, motor_jam, mode_register_darts);
            mqtt.publish(stateTopic, jsonString, 0, 0);
            Serial.println("MQTT report");

        }
    }

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
