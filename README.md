# rfid_nerfgun
A Nerf toy that reads chipped darts, and reports over mqtt

This project uses the PN532 RFID reader and an Espressif ESP8266 (Wemos D1 Mini) to read an Ntag203 RFID tag embedded in each nerf dart.

The read coil is placed just in front of the flywheels, and will apply DC braking to the motors if an unregistered tag passes the coils.

