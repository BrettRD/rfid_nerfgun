#!/usr/bin/python3
import paho.mqtt.client as mqtt
import json

client = mqtt.Client()


# fails if base topic includes '/'
base_topic = "nerfgun"

scanners = dict()

max_darts = 2

def on_connect(client, userdata, flags, rc):
    print("Connected with result code "+str(rc))
    # Subscribing in on_connect() means that if we lose the connection and
    # reconnect then subscriptions will be renewed.
    client.subscribe(base_topic + "/+/state")   # the state of the scanner
    client.subscribe(base_topic + "/+/tag")     # the tags being read
    client.subscribe(base_topic + "/+/lwt")     # the online/offline messages
    client.subscribe(base_topic + "/+/cmd")     # commands being sent to the scanners



# The callback for when a PUBLISH message is received from the server.
def on_message(client, userdata, msg):
    split_topic = msg.topic.split('/')
    
    if split_topic[0] == base_topic:
        if split_topic[1] != "name":    #ignore the name message

            # read the name of the scanner
            scanner_name = split_topic[1]  #this will catch the lwt "online" message too
            if not scanner_name in scanners:
                print("found new scanner: " + scanner_name + " resetting tag list")
                scanners[scanner_name] = dict()
                scanners[scanner_name]["scanned_tags"] = list()
                scanners[scanner_name]["state"] = dict()
                reset_scanner(scanner_name)

            message = json.loads(msg.payload)   #parse the message

            subtopic = split_topic[2]


            if subtopic == "tag":
                tag_id = message["uid"]        
                print("scanner " + scanner_name + " scanned tag " + tag_id)
                scanners[scanner_name]["scanned_tags"].append(tag_id)

            if subtopic == "state":
                scanners[scanner_name]["state"] = message

                if scanners[scanner_name]["state"]["n_darts"] >= max_darts:
                    if scanners[scanner_name]["state"]["register"] == True:
                        stop_register_mode(scanner_name)    # the scanner has registered enough tags, don't register any more
                        print("scanner " + scanner_name + " is full, stopping register")

                if scanners[scanner_name]["state"]["n_darts"] < max_darts:
                    if scanners[scanner_name]["state"]["register"] == False:
                        start_register_mode(scanner_name)    # the scanner has not registered enough darts (usually immediately after boot)
                        print("scanner " + scanner_name + " is not full, starting register")




def stop_register_mode(scanner_name):
    topic = base_topic + "/" + scanner_name + "/cmd"
    client.publish(topic, "{\"register_mode\":false}")

def start_register_mode(scanner_name):
    topic = base_topic + "/" + scanner_name + "/cmd"
    client.publish(topic, "{\"register_mode\":true}")

def reset_scanner(scanner_name):
    topic = base_topic + "/" + scanner_name + "/cmd"
    client.publish(topic, "{\"clear_darts\":true}")




def main():

    client.on_connect = on_connect
    client.on_message = on_message
    client.connect("localhost", 1883, 60)


    # Blocking call that processes network traffic, dispatches callbacks and
    # handles reconnecting.
    # Other loop*() functions are available that give a threaded interface and a
    # manual interface.
    client.loop_forever()



if __name__== "__main__":
  main()
