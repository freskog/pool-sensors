//This code is for the Atlas Scientific wifi pool kit that uses the Adafruit huzzah32 as its computer.

#include <iot_cmd.h>
#include <WiFi.h>                                                //include wifi library 
#include <sequencer4.h>                                          //imports a 4 function sequencer 
#include <sequencer1.h>                                          //imports a 1 function sequencer 
#include <Ezo_i2c_util.h>                                        //brings in common print statements
#include <Ezo_i2c.h> //include the EZO I2C library from https://github.com/Atlas-Scientific/Ezo_I2c_lib
#include <Wire.h>    //include arduinos i2c library
#include <PubSubClient.h>

IPAddress server(10, 10, 100, 11);                                //Your MQTT broker IP

WiFiClient client;                                                //declare that this device connects to a Wi-Fi network,create a connection to a specified internet IP address
PubSubClient mqtt_client(client);

//----------------Fill in your Wi-Fi / MQTT Credentials-------
const String ssid = "YourWIFI-Here";                              //The name of the Wi-Fi network you are connecting to
const String pass = "YourWIFI-PWD";                               //Your WiFi network password
const String mqttUser = "your-mqtt-user";                         //User expected by MQTT broker
const String mqttPassword = "your-mqtt-pwd";                      //Password expected by MQTT broker
//------------------------------------------------------------------

Ezo_board PH = Ezo_board(99, "PH");           //create a PH circuit object, who's address is 99 and name is "PH"
Ezo_board ORP = Ezo_board(98, "ORP");         //create an ORP circuit object who's address is 98 and name is "ORP"
Ezo_board RTD = Ezo_board(102, "RTD");        //create an RTD circuit object who's address is 102 and name is "RTD"

float lastPH = 0;                             //store the ph in a global variable
float lastORP = 0;                            //store the orp in a global variable
float lastTemp = 0;                           //store the temp (°C) in a global variable

Ezo_board device_list[] = {   //an array of boards used for sending commands to all or specific boards
  PH,
  ORP,
  RTD,
};

Ezo_board* default_board = &device_list[0]; //used to store the board were talking to

//gets the length of the array automatically so we dont have to change the number every time we add new boards
const uint8_t device_list_len = sizeof(device_list) / sizeof(device_list[0]);

//enable pins for each circuit
const int EN_PH = 12;
const int EN_ORP = 27;
const int EN_RTD = 15;

const unsigned long reading_delay = 1000;                 //how long we wait to receive a response, in milliseconds
const unsigned long mqtt_delay = 15000;                   //how long we wait to send values to mqtt, in milliseconds

unsigned int poll_delay = 2000 - reading_delay * 2 - 300; //how long to wait between polls after accounting for the times it takes to send readings

float k_val = 0;                                          //holds the k value for determining what to print in the help menu

bool polling  = true;                                     //variable to determine whether or not were polling the circuits
bool send_to_mqtt = true;                                 //variable to determine whether or not were sending data to thingspeak

bool wifi_isconnected() {                                 //function to check if wifi is connected
  return (WiFi.status() == WL_CONNECTED);
}

void reconnect_wifi() {                                   //function to reconnect wifi if its not connected
  if (!wifi_isconnected()) {
    WiFi.begin(ssid.c_str(), pass.c_str());
    Serial.println("connecting to wifi");
  }
}

void reconnect_mqtt() {
    // Loop until we're reconnected

    mqtt_client.setServer(server, 1883);

    while (!mqtt_client.connected()) {
        Serial.print("Attempting MQTT connection...");
        // Attempt to connect
        if (mqtt_client.connect("Pool-Sensors", (char *) mqttUser.c_str(), (char *) mqttPassword.c_str())) {
            Serial.println("connected");
        } else {
            Serial.print("failed, rc=");
            Serial.print(mqtt_client.state());
            Serial.println(" try again in 5 seconds");
            // Wait 5 seconds before retrying
            delay(5000);
        }
    }
}

void mqtt_publish() {
  if (send_to_mqtt == true) {                                                    //if we're datalogging
    if (wifi_isconnected()) {
      if(!mqtt_client.connected()) {
        reconnect_mqtt();
      }
      
      Serial.println("publishing readings");

      mqtt_client.publish("pool/ph", (char *) String(lastPH).c_str());
      mqtt_client.publish("pool/orp", (char *) String(lastORP).c_str());
      mqtt_client.publish("pool/temperature", (char *) String(lastTemp).c_str());
      
      Serial.println("sent to mqtt");
    }
  }
}

void step1();      //forward declarations of functions to use them in the sequencer before defining them
void step2();
void step3();
void step4();
Sequencer4 Seq(&step1, reading_delay,   //calls the steps in sequence with time in between them
               &step2, 300,
               &step3, reading_delay,
               &step4, poll_delay);

Sequencer1 Wifi_Seq(&reconnect_wifi, 10000);  //calls the wifi reconnect function every 10 seconds

Sequencer1 MQTT_seq(&mqtt_publish, mqtt_delay); //sends data to thingspeak with the time determined by thingspeak delay

void setup() {

  pinMode(EN_PH, OUTPUT);                                                         //set enable pins as outputs
  pinMode(EN_ORP, OUTPUT);
  pinMode(EN_RTD, OUTPUT);
  digitalWrite(EN_PH, LOW);                                                       //set enable pins to enable the circuits
  digitalWrite(EN_ORP, LOW);
  digitalWrite(EN_RTD, HIGH);

  Wire.begin();                           //start the I2C
  Serial.begin(9600);                     //start the serial communication to the computer

  WiFi.mode(WIFI_STA);                    //set ESP32 mode as a station to be connected to wifi network
  Wifi_Seq.reset();                       //initialize the sequencers
  Seq.reset();
  MQTT_seq.reset();
}

void loop() {
  String cmd;                            //variable to hold commands we send to the kit

  Wifi_Seq.run();                        //run the sequncer to do the polling

  if (receive_command(cmd)) {            //if we sent the kit a command it gets put into the cmd variable
    polling = false;                     //we stop polling
    send_to_mqtt = false;          //and sending data to thingspeak
    if (!process_coms(cmd)) {            //then we evaluate the cmd for kit specific commands
      process_command(cmd, device_list, device_list_len, default_board);    //then if its not kit specific, pass the cmd to the IOT command processing function
    }
  }

  if (polling == true) {                 //if polling is turned on, run the sequencer
    Seq.run();
    MQTT_seq.run();
  }
}


void step1() {
  //send a read command. we use this command instead of RTD.send_cmd("R");
  //to let the library know to parse the reading
  RTD.send_read_cmd();
}

void step2() {
  receive_and_print_reading(RTD);             //get the reading from the RTD circuit

  if ((RTD.get_error() == Ezo_board::SUCCESS) && (RTD.get_last_received_reading() > -1000.0)) { //if the temperature reading has been received and it is valid
    PH.send_cmd_with_num("T,", RTD.get_last_received_reading());
    lastTemp = RTD.get_last_received_reading();                                                 //assign temperature readings to the third column of thingspeak channel
  } else {                                                                                      //if the temperature reading is invalid
    PH.send_cmd_with_num("T,", 25.0);                                                           //send default temp = 25 deg C to PH sensor
    lastTemp = 25.0;                                                                            //assign temperature default temp.
  }

  Serial.print(" ");
}

void step3() {
  //send a read command. we use this command instead of PH.send_cmd("R");
  //to let the library know to parse the reading
  PH.send_read_cmd();
  ORP.send_read_cmd();
}

void step4() {
  receive_and_print_reading(PH);                                   //get the reading from the PH circuit
  if (PH.get_error() == Ezo_board::SUCCESS) {                      //if the PH reading was successful (back in step 1)
    lastPH = PH.get_last_received_reading();                       //assign PH readings to the first column of thingspeak channel
  }

  Serial.print("  ");

  receive_and_print_reading(ORP);                                   //get the reading from the ORP circuit

  if (ORP.get_error() == Ezo_board::SUCCESS) {                      //if the ORP reading was successful (back in step 1)
    lastORP = ORP.get_last_received_reading();                      //assign ORP readings to the second column of thingspeak channel
  }

  Serial.println();
}

void start_datalogging() {
  polling = true;                                                 //set poll to true to start the polling loop
  send_to_mqtt = true;
  MQTT_seq.reset();
}

bool process_coms(const String &string_buffer) {      //function to process commands that manipulate global variables and are specifc to certain kits
  if (string_buffer == "HELP") {
    print_help();
    return true;
  }
  else if (string_buffer.startsWith("DATALOG")) {
    start_datalogging();
    return true;
  }
  else if (string_buffer.startsWith("POLL")) {
    polling = true;
    Seq.reset();

    int16_t index = string_buffer.indexOf(',');                    //check if were passing a polling delay parameter
    if (index != -1) {                                              //if there is a polling delay
      float new_delay = string_buffer.substring(index + 1).toFloat(); //turn it into a float

      float mintime = reading_delay * 2 + 300;
      if (new_delay >= (mintime / 1000.0)) {                                     //make sure its greater than our minimum time
        Seq.set_step4_time((new_delay * 1000.0) - mintime);          //convert to milliseconds and remove the reading delay from our wait
      } else {
        Serial.println("delay too short");                          //print an error if the polling time isnt valid
      }
    }
    return true;
  }
  return false;                         //return false if the command is not in the list, so we can scan the other list or pass it to the circuit
}

void print_help() {
  Serial.println(F("Atlas Scientific I2C pool kit                                              "));
  Serial.println(F("Commands:                                                                  "));
  Serial.println(F("datalog      Takes readings of all sensors every 15 sec send to thingspeak "));
  Serial.println(F("             Entering any commands stops datalog mode.                     "));
  Serial.println(F("poll         Takes readings continuously of all sensors                    "));
  Serial.println(F("                                                                           "));
  Serial.println(F("ph:cal,mid,7     calibrate to pH 7                                         "));
  Serial.println(F("ph:cal,low,4     calibrate to pH 4                                         "));
  Serial.println(F("ph:cal,high,10   calibrate to pH 10                                        "));
  Serial.println(F("ph:cal,clear     clear calibration                                         "));
  Serial.println(F("                                                                           "));
  Serial.println(F("orp:cal,225          calibrate orp probe to 225mV                          "));
  Serial.println(F("orp:cal,clear        clear calibration                                     "));
  Serial.println(F("                                                                           "));
  Serial.println(F("rtd:cal,t            calibrate the temp probe to any temp value            "));
  Serial.println(F("                     t= the temperature you have chosen                    "));
  Serial.println(F("rtd:cal,clear        clear calibration                                     "));
}
