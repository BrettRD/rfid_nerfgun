

#include <SPI.h>
#include <PN532_SPI.h>
#include <PN532.h>
#include <NfcAdapter.h>

PN532_SPI intf(SPI, 10);
PN532 nfc = PN532(intf);

//uint8_t password[4] =  {0x12, 0x34, 0x56, 0x78};
//uint8_t buf[4];
uint8_t uid[7]; 
uint8_t uidLength;

void setup(void) {
    Serial.begin(115200);
    Serial.println("NTAG21x R/W");

    nfc.begin();
    nfc.SAMConfig();
}

void loop(void) {
    Serial.println("wait for a tag");
    // wait until a tag is present
    while (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength)) {

    }

    Serial.print("uid = ");
    for(uint8_t i=0; i<uidLength; i++){
      Serial.print("0123456789abcdef"[uid[i] >> 4]);
      Serial.print("0123456789abcdef"[(uid[i] & 0x0f)]);
    }
    Serial.println("");
    
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
