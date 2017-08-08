#include <ELClient.h>
#include <ELClientCmd.h>
#include <ELClientMqtt.h>
#include <ELClientResponse.h>
#include <ELClientRest.h>
#include <ELClientSocket.h>
#include <ELClientWebServer.h>
#include <FP.h>

#include <MFRC522.h>
#include <MFRC522Extended.h>

#include <avr/wdt.h>

#include <SPI.h>

#define SS_PIN 10 //SS for RFID
#define RST_PIN 9 // RST for RFID (not connect)
#define LED_Green 8
#define LED_Red 7

String ID = "";

int myTimeout = 50;
byte nuidPICC = 0;
static uint32_t last;
bool flag = false;
#define del 2000

MFRC522 rfid(SS_PIN, RST_PIN); // Instance of the RFID class

/*
esp-link Initialize block
*/
// Initialize a connection to esp-link using the normal hardware serial port both for
// SLIP and for debug messages.
ELClient esp(&Serial, &Serial);

// Initialize CMD client (for GetTime)
ELClientCmd cmd(&esp);

// Initialize the MQTT client
ELClientMqtt mqtt(&esp);

// Callback made from esp-link to notify of wifi status changes
// Here we just print something out for grins
void wifiCb(void *response)
{
    ELClientResponse *res = (ELClientResponse *)response;
    if (res->argc() == 1)
    {
        uint8_t status;
        res->popArg(&status, 1);

        if (status == STATION_GOT_IP)
        {
            Serial.println("WIFI CONNECTED");
        }
        else
        {
            Serial.print("WIFI NOT READY: ");
            Serial.println(status);
        }
    }
}

bool connected;
const char *cardID_mqtt = "AS/FirstDoor/cardID";
const char *server_resp = "AS/FirstDoor/server_response";

// Callback when MQTT is connected
void mqttConnected(void *response)
{
    Serial.println("MQTT connected!");
    //mqtt.subscribe(cardID_mqtt);
    mqtt.subscribe(server_resp);

    connected = true;
}

void mqttDisconnected(void *response)
{
    Serial.println("MQTT disconnected");
    connected = false;
}

void mqttData(void *response)
{
    ELClientResponse *res = (ELClientResponse *)response;

    Serial.print("Received: topic=");
    String topic = res->popString();
    Serial.println(topic);

    Serial.print("data=");
    String data = res->popString();
    Serial.println(data);

    if (topic == server_resp)
    {
        if (data == "yes")
        {

            digitalWrite(LED_Green, HIGH);
            nuidPICC = 0;
            flag = true;
        }
        else if (data == "no")
        {

            digitalWrite(LED_Red, HIGH);
            nuidPICC = 0;
            flag = true;
        }

        last = millis();
        // flag_screen = true;
        return;
    }
}

void mqttPublished(void *response)
{
    Serial.println("MQTT published");
}
//end esp-link Initialize block

//Setup Arduino
void setup()
{
    //esp MQTT setup
    Serial.begin(115200);
    // Sync-up with esp-link, this is required at the start of any sketch and initializes the
    // callbacks to the wifi status change callback. The callback gets called with the initial
    // status right after Sync() below completes.
    esp.wifiCb.attach(wifiCb); // wifi status change callback, optional (delete if not desired)
    bool ok;
    do
    {
        ok = esp.Sync(); // sync up with esp-link, blocks for up to 2 seconds
        if (!ok)
            Serial.println("EL-Client sync failed!");
    } while (!ok);
    Serial.println("EL-Client synced!");

    // Set-up callbacks for events and initialize with es-link.
    mqtt.connectedCb.attach(mqttConnected);
    mqtt.disconnectedCb.attach(mqttDisconnected);
    mqtt.publishedCb.attach(mqttPublished);
    mqtt.dataCb.attach(mqttData);
    mqtt.setup();
    
    Serial.println("EL-MQTT ready");
   
    // wdt_enable(WDTO_2S);

    pinMode(LED_Green, OUTPUT);
    pinMode(LED_Red, OUTPUT);
    digitalWrite(LED_Green, LOW);
    digitalWrite(LED_Red, LOW);

    SPI.begin();     // Init SPI bus
    rfid.PCD_Init(); // Init MFRC522
}

//Main loop
bool flagWDT = true;
void loop()
{
    esp.Process();

    if (!connected && flagWDT)
    {
        wdt_enable(WDTO_4S);
        Serial.println("WatchDog Enabled for 4 sec");
        flagWDT = false;
        return;
    }
    else if (connected && !flagWDT)
    {
        wdt_disable();
        Serial.println("WatchDog disable");
        flagWDT = true;
    }

    if (((millis() - last) > del) & flag)
    {
        nuidPICC = 0;
        //digitalWrite(open_pin, HIGH);
        digitalWrite(LED_Green, LOW);
        digitalWrite(LED_Red, LOW);
        flag = false;
    }

    // Look for new cards
    if (!rfid.PICC_IsNewCardPresent())
        return;

    // Verify if the NUID has been readed
    if (!rfid.PICC_ReadCardSerial())
        return;

    MFRC522::PICC_Type piccType = rfid.PICC_GetType(rfid.uid.sak);

    if (piccType != MFRC522::PICC_TYPE_MIFARE_MINI &&
        piccType != MFRC522::PICC_TYPE_MIFARE_1K &&
        piccType != MFRC522::PICC_TYPE_MIFARE_4K)
    {
        Serial.println(F("Your tag is not of type MIFARE Classic."));
        return;
    }

    if (rfid.uid.uidByte[0] != nuidPICC)
    {
        nuidPICC = rfid.uid.uidByte[0];

        printDec(rfid.uid.uidByte, rfid.uid.size);

        Serial.println(ID);
        char buf[ID.length() + 1];
        ID.toCharArray(buf, ID.length() + 1);
        mqtt.publish(cardID_mqtt, buf);

        last = millis();
        flag = true;
    }
    // Halt PICC
    rfid.PICC_HaltA();

    // Stop encryption on PCD
    rfid.PCD_StopCrypto1();
}

void printDec(byte *buffer, byte bufferSize)
{
    ID = "";
    for (byte i = 0; i < bufferSize; i++)
    {
        ID.concat(String(buffer[i], DEC));
    }
}