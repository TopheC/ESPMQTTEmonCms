/*
  //20191002 : Entierement fonctionnel, publish et subscribe
  //20191003 : erreur pas de publication sur MQTT
  //20191006 : publication MQTT OK pour Temp1
  //20191007 : publication MQTT OK pour Temp1 et Temp2
  //20191007 : deepsleep ok, reveil ok, pubsub ok
  //20191008 : alim en 3.7 li-ion testé OK
  //20191013 : ajustement des pauses pour coupures propres des liaisons sans erreurs
  //20191013 : ajout de l'envoi d'une temp vers emoncms
  //20191014 : ajout de la gestion de la batterie
  //20191015 : ajout de l'envoi des 2 temp et VBatt emoncms (OK)
  //20191018 : v2 Débug envoi vers emoncms (OK)
  //20191020 : v3 Ajout de NTP pour gestion du temps (OK mais NOK sur la gesion de l'heure d'été (daylight saving))
  //20191020 : Ajout du support de SPIFFS pour logguer des valeur en local sur l'ESP (OK mais dévalidé car plante la gestion EEPROM.h))
  //20191023 : Optimisations du code et boucles.  

*/

/* ############### Libraries */

#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>
#include <PubSubClient.h>
#include <NTPClient.h> // for NTP
#include <WiFiUdp.h> // NTP uses UDP.
//#include <FS.h> // Support de la mémoire SPIFFS


/* ############### Gestion du mode DEBUG */
// Should we enable debugging (via serial-console output) ?
//
// Use either `#undef DEBUG`, or `#define DEBUG`.
//

#define DEBUG

//
// If we did then DEBUG_LOG will log a string, otherwise
// it will be ignored as a comment.
//
#ifdef DEBUG
#  define DEBUG_LOG(x) Serial.print(x)
#else
#  define DEBUG_LOG(x)
#endif


/* ################ Déclarations Constantes */

// Bus One Wire sur la pin 4 de l'arduino
#define ONE_WIRE_BUS 4

// Résolution/precisions possibles: 9,10,11,12
#define TEMPERATURE_PRECISION 10

// Time to sleep (in seconds):
const int sleepTimeS = 60;

// Port MQTT utilisé sur CloudMQTT
#define MQTT_PORT 16140


/*  ################ Déclarations Objets */

// UDP-socket & local-port for replies.
//
WiFiUDP ntpUDP;
unsigned int localPort = 2390;

//
// The NTP-server we use.
//
//NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3600, 60000);
NTPClient timeClient(ntpUDP, "0.fr.pool.ntp.org", 0, 300000);


// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

// Tableaux contenant l'adresse de chaque sonde OneWire | arrays to hold device addresses
DeviceAddress SondeTemp1 = { 0x28,  0x15,  0x6,  0x13,  0x5,  0x0,  0x0,  0xB7 };
DeviceAddress SondeTemp2 = { 0x28,  0x41,  0xE6,  0xD0,  0x4,  0x0,  0x0,  0x85 };




// structure pour la la lecture de la configuration Wifi mémorisée en EEPROM
struct EEconf {
  char WifiSsid[32];
  char WifiPassword[64];
  char EspHostname[32];
  char MQTTServer[64];
  char MQTTUser[32];
  char MQTTPassword[64];
} readconf;

// nom de la machine ayant le broker MQTT (en mDNS)
//const char* mqtt_server = "farmer.cloudmqtt.com";
const char* mqtt_server = readconf.WifiSsid;

//const char* outTopic1 = "sensor/temperature1";
//const char* inTopic1 = "sensor/temperature1";
const char* outTopic1 = "Maison/Cellier/Temperature1";
const char* inTopic1 = "Maison/Cellier/Temperature1";
const char* outTopic2 = "Maison/Cellier/Temperature2";
const char* inTopic2 = "Maison/Cellier/Temperature2";
#define battery_topic "Maison/Cellier/Batterie" // send battery voltage

float Temp1 = 0;
float Temp2 = 0;
float TemperatureInterieure = 0;
char message_temperature[5] = "";
const char* node = "Cellier";
const char* flux0 = "Maison-Cellier-Vbat";
const char* flux1 = "Maison-Cellier-Temperature1";
const char* flux2 = "Maison-Cellier-Temperature2";


// Config emoncms
const char* host = "51.158.73.126";
const char* streamId   = "....................";
const char* apikey = "31d8373fa235b023a3018146093f2537";


/* ############### Connection au WIFI */

// objet pour la connexion
WiFiClient espClient;    // Create an ESP8266 WifiClient class to connect to the MQTT server
// connexion MQTT
PubSubClient client(espClient);
// pour l'intervalle
long lastMsg = 0;
// valeur à envoyer
byte val = 0;

/* ############### Fonction de gestion du WIFI  */
void setup_wifi() {
  // mode station
  WiFi.mode(WIFI_STA);
  Serial.println();
  Serial.print("Connexion : ");
  Serial.println(readconf.WifiSsid);
  // connexion wifi
  WiFi.begin(readconf.WifiSsid, readconf.WifiPassword);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }

  // affichage
  Serial.println("");
  Serial.println("Connexion wifi ok");
  Serial.print("Adresse IP: ");
  Serial.println(WiFi.localIP());

  // configuration mDNS
  WiFi.hostname(readconf.EspHostname);
  if (!MDNS.begin(readconf.EspHostname)) {
    Serial.println("Erreur configuration mDNS!");
  } else {
    Serial.println("répondeur mDNS démarré");
    Serial.println(readconf.EspHostname);
  }
}


/* ############### Fonction callback MQTT  */
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message [");
  Serial.print(topic); //Print the Topic
  Serial.print("] ");
  // affichage du payload
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  // le caractère '1' est-il le premier du payload ?
  if ((char)payload[0] == '1') {
    // oui led = on
    digitalWrite(LED_BUILTIN, LOW); // Turn the LED on (Note that LOW is the voltage level
    // but actually the LED is on; this is because
    // it is active low on the ESP-01)
  } else {
    // non led = off
    digitalWrite(LED_BUILTIN, HIGH);  // Turn the LED off by making the voltage HIGH
  }
}

/* ############### Fonction connexion et abonnement MQTT  */
void reconnect() {
  // Connecté au broker ?
  while (!client.connected()) {
    // non. On se connecte.
    if (!client.connect(readconf.EspHostname, readconf.MQTTUser, readconf.MQTTPassword)) {
      Serial.print("Erreur connexion MQTT, rc=");
      Serial.println(client.state());
      delay(5000);
      continue;
    }
    Serial.println("Connexion serveur MQTT ok");
    // connecté.
    // on s'abonne au topic "outTopic1"
    client.subscribe(outTopic1);
    Serial.println("Subscribe ok");
    // on s'abonne au topic "outTopic2"
    client.subscribe(outTopic2);
    Serial.println("Subscribe ok");
  }
}


/* ############################################## SETUP ###################################################### */
// put your setup code here, to run once:

void setup() {

  Serial.begin(115200);

  // Démarrage du client NTP - Start NTP client ........................................................
  timeClient.begin();
  // Set offset time in seconds to adjust for your timezone, for example:
  // GMT +1 = 3600
  // GMT +8 = 28800
  // GMT -1 = -3600
  // GMT 0 = 0
  timeClient.setTimeOffset(3600);

  /* Désactivé car en conflit apparent avec la gestion EEPROM des variables 'en dur" par ESP

    // demarrage file system SPIFFS .......................................................................
    SPIFFS.begin();
    Serial.println("Demarrage file System");

    // Lecture du ficher test.txt
    File dataFile = SPIFFS.open("/test.txt", "r");   //Ouverture fichier pour le lire
    Serial.println("Lecture du fichier en cours:");
    //Affichage des données du fichier
    for(int i=0;i<dataFile.size();i++)
    {
      Serial.print((char)dataFile.read());    //Read file
    }
    dataFile.close();
  */

  // Lecture des valeurs des sondes de température ......................................................

  // Start up the library
  sensors.begin();

  // locate devices on the bus
  Serial.print("Locating devices...");
  Serial.print("Found ");
  Serial.print(sensors.getDeviceCount(), DEC);
  Serial.println(" devices.");

  // report parasite power requirements
  Serial.print("Parasite power is: ");
  if (sensors.isParasitePowerMode()) Serial.println("ON");
  else Serial.println("OFF");

  // Vérifie sir les capteurs sont connectés | check and report if sensors are connected
  if (!sensors.getAddress(SondeTemp1, 0)) Serial.println("Unable to find address for Device 0");
  if (!sensors.getAddress(SondeTemp2, 1)) Serial.println("Unable to find address for Device 1");

  // set the resolution to 10 bit per device
  sensors.setResolution(SondeTemp1, TEMPERATURE_PRECISION);
  sensors.setResolution(SondeTemp2, TEMPERATURE_PRECISION);

  // On vérifie que le capteur sont correctement configuré | Check that ensor is correctly configured
  Serial.print("Device 0 Resolution: ");
  Serial.print(sensors.getResolution(SondeTemp1), DEC);
  Serial.println();
  Serial.print("Device 1 Resolution: ");
  Serial.print(sensors.getResolution(SondeTemp2), DEC);
  Serial.println();

  // configuration led ..............................................
  pinMode(LED_BUILTIN, OUTPUT);
  // configuration moniteur série
  Serial.begin(115200);
  // configuration EEPROM
  EEPROM.begin(sizeof(readconf));
  // lecture configuration
  EEPROM.get(0, readconf);
  // connexion wifi
  setup_wifi();
  // connexion broker
  client.setServer(mqtt_server, MQTT_PORT);
  // configuration callback
  client.setCallback(callback);
}

// Fonction Print Temperature .....................................................
void printTemperature(String label, DeviceAddress deviceAddress) {
  float tempC = sensors.getTempC(deviceAddress);
  Serial.print(label);
  if (tempC == -127.00) {
    Serial.print("Error getting temperature");
  } else {
    //Serial.print(" Temp C: ");
    Serial.print(tempC);
    //Serial.print(" Temp F: ");
    //Serial.println(DallasTemperature::toFahrenheit(tempC));
  }
}

// Fonction Print Vbat .....................................................
void battery()
{
  // read the input on analog pin 0:
  int sensorValue = analogRead(A0);
  // Convert the analog reading (which goes from 0 - 1023) to a voltage (0 - 4.5V):
  float voltage = 4.4 * (sensorValue / 1023.0);
  // send out the value you read:
  Serial.print("Voltage: ");
  Serial.println(voltage);
  client.publish(battery_topic, String(voltage).c_str(), true);
}


// Publish temp on MQTT ..................................................
//void publishSerialData(char *serialData) {
//  if (!client.connected()) {
//    reconnect();
//  }
//  Serial.println();
//  client.publish(outTopic1, serialData);
//}



/* ############################################## LOOP ###################################################### */
// put your main code here, to run repeatedly:

void loop() {
  Serial.println("Début de loop");
  Serial.println("Croquis Espmqtt_auth3.ino");

  // Met à jour l'heure toutes les x secondes - update time every x secondes
  timeClient.update();
  Serial.println(timeClient.getFormattedTime());

  //Serial.print("Requesting temperatures...");
  sensors.requestTemperatures();
  //Serial.println("DONE");
  Temp1 = sensors.getTempC(SondeTemp1);
  Temp2 = sensors.getTempC(SondeTemp2);

  // array pour conversion val
  char msg1[16];
  char msg2[16];

  // array pour topic
  char topic1[64];
  char topic2[64];

  // Sommes-nous connecté ?
  if (!client.connected()) {
    // non. Connexion
    reconnect();
    Serial.println("Reconnect ok");
  }
  // gestion MQTT
  client.loop();
  //Serial.println("loop");


  // temporisation
  long now = millis();
  if (now - lastMsg > 3000) {

    // 3s de passé
    Serial.println("3s de passé sur la tempo");
    lastMsg = now;
    val++;
    Serial.println("Compteur : " + String(val));
    //Serial.println(Temp1);


    // construction message
    //sprintf(msg, "hello world #%hu", val);
    //float data = printTemperature("", SondeTemp1)
    //dtostrf(Temp1, 5, 2, data));
    //sprintf(Temp1, TemperatureInterieure)
    sprintf(msg1, String(Temp1).c_str());
    sprintf(msg2, String(Temp2).c_str());


    // construction topic
    // sprintf(topic, "maison/%s/valeur", readconf.myhostname);
    sprintf(topic1, "Maison/Cellier/Temperature1", readconf.EspHostname);
    sprintf(topic2, "Maison/Cellier/Temperature2", readconf.EspHostname);


    // affichage message du topic
    Serial.println(topic1);
    Serial.println(msg1);

    // publication message sur topic
    client.publish(topic1, msg1);
    client.publish(topic2, msg2);


    // publishSerialData;
    //printTemperature("Inside : ", Temp1);
    printTemperature("Temp1 : ", SondeTemp1);
    Serial.println();
    printTemperature("Temp2 : ", SondeTemp2);
    Serial.println();

    // read battery level
    battery();

    // Pause de 10s pour temps de publication MQTT.
    delay(10000);

    /* envoi data vers emoncms ................................................................*/
    // Serial.print("temperature float:");
    //Serial.println(celsius);

    // read battery level
    //battery();  // read battery level


    // read the input on analog pin 0:
    int sensorValue = analogRead(A0);
    // Convert the analog reading (which goes from 0 - 1023) to a voltage (0 - 4.5V):
    float voltage = 4.4 * (sensorValue / 1023.0);


    /*******  Début prépa Temp1 vers emoncms *******/

    char outstr1[15];
    dtostrf(Temp1, 4, 2, outstr1);  //float to char  4 numero de caracteres  3 cifras sin espacio
    String valor1 = outstr1;  // char to string
    Serial.print("temperature String:");
    Serial.println(valor1);

    char outstr2[15];
    dtostrf(Temp2, 4, 2, outstr2);  //float to char  4 numero de caracteres  3 cifras sin espacio
    String valor2 = outstr2;  // char to string
    Serial.print("temperature String:");
    Serial.println(valor2);

    char outstr3[15];
    dtostrf(voltage, 4, 2, outstr3);  //float to char  4 numero de caracteres  3 cifras sin espacio
    String valor3 = outstr3;  // char to string
    Serial.print("VBat String:");
    Serial.println(valor3);

    String url1 = "/input/post?node=" + String(node) + "&json={" + String(flux1) + ":" + valor1 + "," + String(flux2) + ":" + valor2 + "," + String(flux0) + ":" + valor3 + "}&apikey=" + apikey;

    Serial.print("Requesting URL: ");
    Serial.println(url1);

    /*******  Fin prépa Temp1 vers emoncms *******/

    /*******  Début prépa Temp2 vers emoncms *******

        char outstr2[15];
        dtostrf(Temp2,4, 2, outstr2);   //float to char  4 numero de caracteres  3 cifras sin espacio
        String valor2= outstr2;   // char to string
        Serial.print("temperature String:");
        Serial.println(valor2);

        String url2 = "/emoncms/input/post?node="+String(node)+"&json={"+String(flux2)+":"+ valor2 +"}&apikey="+apikey;

      Serial.print("Requesting URL: ");
      Serial.println(url2);

    *******  Fin prépa Temp2 vers emoncms *******/

    /*******  Début envoi Temp1 et Temp2 vers emoncms *******/

    // Use WiFiClient class to create TCP connections
    WiFiClient client;
    const int httpPort = 80;
    if (!client.connect(host, httpPort)) {
      Serial.println("connection failed");
      return;
    }

    // This will send the request Temp1 to the server
    client.print(String("GET ") + url1 + " HTTP/1.1\r\n" +
                 "Host: " + host + "\r\n" +
                 "Connection: close\r\n\r\n");
    /*
      delay(2000);

      // This will send the request Temp2 to the server
      client.print(String("GET ") + url2 + " HTTP/1.1\r\n" +
                   "Host: " + host + "\r\n" +
                   "Connection: close\r\n\r\n");

      delay(2000);
    */

    /*******  Fin envoi Temp1 et Temp2 vers emoncms *******/

    // Read all the lines of the reply from server and print them to Serial
    while (espClient.available()) {
      String line = espClient.readStringUntil('\r');
      Serial.print(line);
    }

    Serial.println();
    Serial.println("closing connection");
    /* envoi data vers emoncms ................................................................*/





    // Affichage Sleep
    Serial.println();
    Serial.println("ESP8266 in sleep mode pour " + String(sleepTimeS) + "s");

    // Pause de 3s avant coupure liaison.
    delay(3000);

    // Passage en Mode Deepsleep (en µs) pour sleepTimeS seconde(s).
    ESP.deepSleep(sleepTimeS * 1e6);

    // Pause de 3s avant coupure liaison.
    delay(3000);
  }
}
