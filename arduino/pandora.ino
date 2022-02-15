/*
   Pandora.cpp
   Copyright (C) 2021-2022 Matteo Piacentini
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <WiFi.h>
#include <DHT.h>
#include <Thread.h>
#include <ThreadController.h>
#include <StaticThreadController.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <Logger.h> 

// Define pin
#define HUMIDITY_SOIL_SENSOR_PIN 35
#define ENVIRONMENT_SENSOR_PIN 27
#define BRIGHTNESS_SENSOR_PIN 34
#define IRRIGATION_PUMP_PIN 32
#define FLOAT_SWITCH_SENSOR_PIN 33  
#define CTRL_RED_PIN 18
#define CTRL_GREEN_PIN 19
#define CTRL_BLUE_PIN 21

// Define softAP wifi default configurations
#define LOCAL_AP_SSID "PandoraWiFi"
#define LOCAL_AP_PASSWORD "Secret1234"
// NOTE! This value must be unique in each sketch uploaded
// This is an example uuid
#define UNIQUE_GENERATED_UUID_AS_ID "d1tM8gy6LkO2xi8BwF9PhQ"

// Define default 
IPAddress local_IP(192,168,4,22);
IPAddress gateway(192,168,4,9);
IPAddress subnet(255,255,255,0);

// Define mqtt connection client
const char* mqtt_server = "broker.emqx.io";
const unsigned int mqtt_port = 1883;

// This struct contains wifi credentials 
struct init_fmt {
  char ssid[63], passwd[63];      // Define wifi connection credentials
  char username[256];
  uint32_t identifier;            // Define pot identifier
  char denomination[256];         // Define this pot's denomination
  char ntpServer[256];
  long gmtOffset_sec; 
  int daylightOffset_sec;
};

// This struct contains sensor data
struct snr_fmt { 
  uint16_t dryness, brightness; 
  float env_humidity, env_temperature; 
  uint8_t capacity;
  char padding[3];
  time_t origin;
};

// This struct contains history of latest commands
struct cmd_fmt {
    
  // This struct contains command for light
  struct light_t {
    uint8_t rgb;
    char padding[3];    // Don't remove this value or unpack doesn't work well
    uint32_t limit;     // Limit expressed in s
    time_t origin;
  } l_t;

  // This struct contains command for irrigation
  struct drain_t {
    bool is_on;
    char padding[3];
    uint32_t limit;     // Limit expressed in ml
    time_t origin;
  } d_t;

  struct autopilot_t {
      
    bool is_on;
    char padding[3];
      
    struct drain_autopilot_t {
      uint16_t dryness_max;
      char padding[2];
      // Restricts AI decisions to only the defined range
      // (i.e from 8.30 am to 9 pm)
      uint8_t start_h, start_m;
      uint8_t end_h, end_m;
    } drain_a_t;
      
    struct light_autopilot_t {
      uint16_t brightness_min;
      char padding[2];
      // Restricts AI decisions to only the defined range
      uint8_t start_h, start_m;
      uint8_t end_h, end_m;
    } light_a_t;
      
    time_t origin;
      
  } a_t;

  struct force_update_t {
    bool is_on;
    char padding[3];    // Don't remove
    time_t origin;
  } fu_t;

};

// Define some public variables
struct snr_fmt sensor_data;
struct cmd_fmt cfh;
struct init_fmt ini;


// Define some utils function
static bool inTime(uint8_t sHour, uint8_t sMin, uint8_t eHour, uint8_t eMin){
  // Convert local hour:minutes into seconds starting from midnight
  auto toSeconds = [](uint8_t h, uint8_t m){ return (h * 60 * 60) + (m * 60); };
  // Get local time
  time_t current = time(NULL);
  struct tm* local = localtime(&current);
  // Hours are expressed in range(0, 23) and minutes are expressed in range(0, 59)
  if ((toSeconds(local->tm_hour, local->tm_min) >= toSeconds(sHour-1, sMin-1)) && 
      (toSeconds(local->tm_hour, local->tm_min) < toSeconds(eHour-1, eMin-1)))
    return true;
  return false;
}


class SensorThreadController : public StaticThreadController<4> {

  // Threads defined here should have a shorter interval duration than that set for the 
  // autopilot threads, otherwise those may not work properly.
  
  private:
    
    class EnvironmentSensorThread : public Thread {
  
        public:

          DHT* dht;
  
          EnvironmentSensorThread() {
            // Reading temperature or humidity takes about 250 milliseconds!    
            // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)  
            // Repeat this task every second.    
            interval = 1000;
            // Initialize dht sensor
            dht = new DHT(ENVIRONMENT_SENSOR_PIN, DHT11);
            dht->begin();
         }
    
         void run() {
            // Read sensor
            sensor_data.env_humidity = dht->readHumidity();  
            sensor_data.env_temperature = dht->readTemperature();
            // Log read action
            Logger::verbose("EnvirnomentalSensorThread/run", "Read DHT11 sensor");
            runned();
          }
    
    };

    class HumiditySoilSensorThread : public Thread {

      public:

        HumiditySoilSensorThread() { 
          // Configure pin mode
          pinMode(HUMIDITY_SOIL_SENSOR_PIN, INPUT);
          // Repeat this task every second;
          interval = 1000; 
        }
    
        void run() {
          // Read sensor
          sensor_data.dryness = analogRead(HUMIDITY_SOIL_SENSOR_PIN);  
          // Log read action
          Logger::verbose("HumiditySoilSensorThread/run", "Read moisture humidity sensor");
          runned();
        }
    
    };

    class BrightnessSensorThread : public Thread {

      public:

        BrightnessSensorThread() { 
          // Configure pinMode
          pinMode(BRIGHTNESS_SENSOR_PIN, INPUT);
          // Repeat this task every second.
          interval = 1000; 
        }
    
        void run() {
          // Read sensor
          sensor_data.brightness = analogRead(BRIGHTNESS_SENSOR_PIN);
          // Log
          Logger::verbose("BrightnessSensorThread/run", "Read brightness from photoresistor");
          runned();
        }
    
    };

    class FloatSwitchSensorThread : public Thread {

      public:

        FloatSwitchSensorThread() { 
          // Configure pin mode
          pinMode(FLOAT_SWITCH_SENSOR_PIN, INPUT);
          // Repeat this task every second.
          interval = 1000; 
        }

        void run() {
          // Read sensor
          int plenty_water_level = analogRead(FLOAT_SWITCH_SENSOR_PIN);
          // Map trash values into true or false
          sensor_data.capacity = map(plenty_water_level, 0, 2048, 0, 1);
          // Log
          Logger::verbose("FloatSwitchSensorThread/run", "Read water capacity from switch sensor");
          runned();
        }
        
    };
    
  public:
  
    // Define threads
    EnvironmentSensorThread envThread;
    HumiditySoilSensorThread soilThread;
    BrightnessSensorThread brightnessThread;
    FloatSwitchSensorThread floatSwitchThread;

    SensorThreadController() : StaticThreadController(&envThread, &soilThread, &brightnessThread, &floatSwitchThread){}

};


class ConnectionThreadsController : public ThreadController {

  public:

    // Define standard mqtt message format
    struct mqtt_standard_fmt {
      
      struct {
        uint8_t tos;
        char padding[3];
        uint32_t identifier;
      } header;
      
      union {
        struct snr_fmt sensor_data;
        struct cmd_fmt::light_t l_t;
        struct cmd_fmt::drain_t d_t;
        struct cmd_fmt::autopilot_t a_t;
        struct cmd_fmt::force_update_t fu_t;
      };
      
    };

    class MQTTConnectionThreadsController : public ThreadController {

      // Reconnection thread and ListeningThread should have short interval duration because 
      // no published messages should be lost due to disconnection from server or missing loop.
      // Sender thread can have arbitrary interval duration.

      public:

        class SenderThread : public Thread {

          private:

            #define UPDATE 0

            // Same standard as mqtt_standard_fmt
            struct sensor_standard_fmt {
              private:
                uint8_t tos = UPDATE;
                char padding[3];
                uint32_t identifier = ini.identifier;
              public:
                struct snr_fmt sensor_data;
            };

            const String topic = String("pandora/") + String(ini.username) + "/" + String(ini.denomination);

          public:

            PubSubClient *mqttClient;
            struct sensor_standard_fmt snr_out_fmt;
            
            SenderThread(PubSubClient *mqttClient) : Thread() {
              // Repeat this task every 15 minutes
              this->interval = 1000 * 60 * 15;
              this->mqttClient = mqttClient;
           }

           void run() {
              // Set current time
              time(&sensor_data.origin);
              // 
              snr_out_fmt.sensor_data = sensor_data;
              if (this->mqttClient->connected()){
              // Publish sensor data to topic
                if (this->mqttClient->publish(topic.c_str(), (uint8_t*) &snr_out_fmt, 
                      sizeof(struct sensor_standard_fmt), false)){
                  // Log if message is published
                  Logger::verbose("SenderThread/run", "Message successfully published on topic");              
                } else {
                  Logger::error("SenderThread/run", "Error while publishing the message!");
                }
              }
              runned();
           }
           
        };

        class ListeningThread : public Thread {

          private:

            #define UPDATE 0
            #define LIGHT 1
            #define DRAIN 2
            #define AUTO 3
            #define FORCE_UPDATE 4

            static void onRequestReceived(char* topic, byte* payload, unsigned int length) {
              // Unpack message
              struct mqtt_standard_fmt* cmd_in = (struct mqtt_standard_fmt*) payload;
              // Log on message received
              Logger::notice("ListeningThread/onRequestReceived", "Command received");
              // Unpack to respective struct
              switch(cmd_in->header.tos){
                case UPDATE:
                  // Do nothing
                  break;
                case LIGHT:
                  // Payload length without the header of the 
                  // message should be equal to command struct size
                  // else jump to error
                  if (length - sizeof(mqtt_standard_fmt::header) != sizeof(struct cmd_fmt::light_t))
                    goto error; 
                  // Continue
                  memcpy(&cfh.l_t, &cmd_in->l_t,  sizeof(struct cmd_fmt::light_t)); 
                  break;
                case DRAIN:
                  // Jump to error
                  if (length - sizeof(mqtt_standard_fmt::header) != sizeof(struct cmd_fmt::drain_t))
                    goto error; 
                  // Continue
                  memcpy(&cfh.d_t, &cmd_in->d_t,  sizeof(struct cmd_fmt::drain_t)); 
                  break;
                case AUTO:
                  // Jump to error
                  if (length - sizeof(mqtt_standard_fmt::header) != sizeof(struct cmd_fmt::autopilot_t))
                    goto error; 
                  // Continue
                  memcpy(&cfh.a_t, &cmd_in->a_t,  sizeof(struct cmd_fmt::autopilot_t)); 
                  break;
                case FORCE_UPDATE:
                  // Jump to error
                  if (length - sizeof(mqtt_standard_fmt::header) != sizeof(struct cmd_fmt::force_update_t))
                    goto error; 
                  // Continue
                  memcpy(&cfh.fu_t, &cmd_in->fu_t, sizeof(struct cmd_fmt::force_update_t));
                  break;
                default:
                  error:
                    // Log invalid message
                    Logger::warning("ListeningThread/onRequestReceived", "Invalid command received");
                    return;
              }
            }

          public:
          
            PubSubClient *mqttClient;

            ListeningThread(PubSubClient *mqttClient) {
              // This should be called regularly to allow the client to process 
              // incoming messages and maintain its connection to the server.
              this->interval = 500;
              // Set client and its callback 
              this->mqttClient = mqttClient;
              this->mqttClient->setCallback(ListeningThread::onRequestReceived);
            }

            void run() {
              // Loop
              this->mqttClient->loop();
              runned();
            }

          
        };

        class ReconnectionThread : public Thread {

          private:
            
            const String client_id = String(ini.identifier);
            const String topic = String("pandora/") + String(ini.username) + "/" + String(ini.denomination);

          public:

            PubSubClient *mqttClient;

            ReconnectionThread(PubSubClient *mqttClient) {
              /* Repeat every minute in order to check the connection status */
              this->interval = 1000 * 60;
              // Set client and its callback 
              this->mqttClient = mqttClient;
            }

            void run() {
              // If disconnected, try to reconnect
              if (!this->mqttClient->connected()){
                // Log connection attempt
                Logger::verbose("ReconnectionThread/run", "Connecting to mqtt server ...");
                if (this->mqttClient->connect(client_id.c_str())){
                  // If reconnection successful, then subscribe to the topic
                  if (this->mqttClient->subscribe(topic.c_str())){
                    // Log successful subscription
                    Logger::verbose("ReconnectionThread/run", "Successful subscription to mqtt's topic");
                  } else {
                    // Log error on subscription to topic
                    Logger::error("ReconnectionThread/run", "Subscription error");
                  }
                } else {
                  // Log error connecting to mqtt server
                 Logger::error("ReconnectionThread/run", ("Server connection error: " + String(this->mqttClient->state())).c_str());
                }
              }              
              runned();
            }
      
        };
        
        // Declare threads 
        ListeningThread *listeningThread;
        ReconnectionThread *reconnThread;
        SenderThread *senderThread;
        // Declare required mqtt connection variables
        WiFiClient /*WiFiClientSecure*/ *espClient;
        PubSubClient *mqttClient;
        
        MQTTConnectionThreadsController() : ThreadController() {

          espClient = new /*WiFiClientSecure()*/ WiFiClient();
          // you can use the insecure mode, when you want to avoid the certificates
          // espClient->setInsecure();
          
          mqttClient = new PubSubClient(*espClient);
          mqttClient->setServer(mqtt_server, mqtt_port);

          this->add((listeningThread = new ListeningThread(mqttClient)));
          this->add((reconnThread = new ReconnectionThread(mqttClient)));
          this->add((senderThread = new SenderThread(mqttClient)));
          
        }

        void run() {

          if (listeningThread->shouldRun()) 
            listeningThread->run(); 

          if (reconnThread->shouldRun())
            reconnThread->run(); 

          // If force update is set to on, then force this thread to execute
          if (senderThread->shouldRun() || cfh.fu_t.is_on){
            senderThread->run();
            // Set force_update to false
            cfh.fu_t.is_on = false;
          }
          
        }
    
    };


    class InitConnectionThreadsController : public ThreadController {

      public:

        class ReconnectionThread : public Thread {

          public:
          
            WiFiClient *old_client;
            WiFiServer *server;

            ReconnectionThread(WiFiServer *server, WiFiClient *old_client){
              this->old_client = old_client;
              this->server = server;
              // Repeat this task every second.
              this->interval = 1000;
            }

            void run() {
              // Listen for new clients
              WiFiClient new_client = server->available();
              if (old_client == NULL || !old_client->connected()){
                // Log
                Logger::notice("ReconnectionThread/run", "New client requests connection to access point");
                // If new client is available
                if (new_client) { 
                  *old_client = new_client; 
                };
              }
              runned();
            }
          
          };

        class ListeningThread : public Thread {

          public:
            
            WiFiClient *new_client;
            WiFiServer *server;

            ListeningThread(WiFiServer *server, WiFiClient *new_client){
              this->new_client = new_client;
              this->server = server;
              // Repeat this task every second
              this->interval = 1000;
            }

            void run() {
              if (new_client != NULL && new_client->connected()){
                // Listen for incoming messages
                if (new_client->available()){
                  // Read & unpack directly
                  new_client->read((uint8_t*) &ini, sizeof(struct init_fmt));
                  // Log new packet received
                  Logger::notice("ListeningThread/run", "Received new configuration");
                }
              }
              runned();
            }
          
        };

        // Declare threads
        ListeningThread *listeningThread;
        ReconnectionThread *reconnThread;
        // Declare tcp connection variables
        WiFiServer *server;
        WiFiClient *new_client;
        
        InitConnectionThreadsController() : ThreadController() {
          // Server should be able to handle an enctypted connection
          server = new WiFiServer(8888);
          new_client = new WiFiClient();
          // 
          this->add((listeningThread = new ListeningThread(server, new_client)));
          this->add((reconnThread = new ReconnectionThread(server, new_client)));
        }

        void startServer() {
          server->begin();
        }

        void stopClient() {
          new_client->stop();
        }
        
    };
  
    InitConnectionThreadsController initConn;
    MQTTConnectionThreadsController mqttConn;
    
    ConnectionThreadsController() : ThreadController() {
      // NOTE! Don't add any thread to this controller 
    }

};


class ActuatorsThreadController : public StaticThreadController<2> {

  // The threads defined in this controller should have very short interval duration 
  // because they are independent threads (respect to others) and as commands issued must be executed immediately.
  
  public:

    class DrainThread : public Thread {

      public:

        time_t start_draining = 0;
        const float time_required_to_fill_up_10ml = 1.1; // s

        DrainThread() { 
          // Set pin mode
          pinMode(IRRIGATION_PUMP_PIN, OUTPUT); 
          // Repeat this task every second.
          this->interval = 1000;
        }


        void run() {
          // Check if there is water in the container
          if (cfh.d_t.is_on && sensor_data.capacity == 1) {
            // Start irrigation
            if (start_draining < cfh.d_t.origin){
              digitalWrite(IRRIGATION_PUMP_PIN, HIGH);
              time(&start_draining);
              // Log start draining
              Logger::verbose("DrainThread/run", "Start draining ...");
            } else {
              // If irrigation is already start, wait
              if ((time(NULL) - start_draining) > ((cfh.d_t.limit / 10) * time_required_to_fill_up_10ml)) {
                // Stop irrigation
                digitalWrite(IRRIGATION_PUMP_PIN, LOW);
                cfh.d_t.is_on = false;
                // Reset start_draining timer
                start_draining = 0;
                // Log stop draining
                Logger::verbose("DrainThread/run", "Stop draining");
              }   
            } 
          } else {
            // Stop irrigation
            digitalWrite(IRRIGATION_PUMP_PIN, LOW);
          }
          runned(); 
        }
        
    };

    class LightThread : public Thread {

      public: 

        #define RED_MASK 0x1
        #define GREEN_MASK 0x2
        #define BLUE_MASK 0x4

        time_t start_lighting = 0;
        
        LightThread() { 
          // Set color controller pin
          pinMode(CTRL_RED_PIN, OUTPUT);
          pinMode(CTRL_GREEN_PIN, OUTPUT);
          pinMode(CTRL_BLUE_PIN, OUTPUT);
          // Repeat this task every second.
          this->interval = 1000;
        }

        void multiDigitalWrite(uint8_t rgb){
          // Enable pins
          digitalWrite(CTRL_RED_PIN, rgb & RED_MASK ? HIGH : LOW);
          digitalWrite(CTRL_GREEN_PIN, rgb & GREEN_MASK ? HIGH : LOW);
          digitalWrite(CTRL_BLUE_PIN, rgb & BLUE_MASK ? HIGH : LOW);
        }
        
        void run() {
          //
          if (cfh.l_t.rgb & RED_MASK || cfh.l_t.rgb & GREEN_MASK || cfh.l_t.rgb & BLUE_MASK){
            // Execute only when new command is received
            if (start_lighting < cfh.l_t.origin){
              // Set time
              time(&start_lighting);
              multiDigitalWrite(cfh.l_t.rgb);
              // Log turn on leds
              Logger::verbose("LightThread/run", "Turn on leds");
            } else {
              // If time expressed by limit is expired then
              if (start_lighting + cfh.l_t.limit < time(NULL)){
                multiDigitalWrite(0x0);
                cfh.l_t.rgb = 0x0; 
                // Reset start lighting timer 
                start_lighting = 0;
                // Log turn off leds
                Logger::verbose("LightThread/run", "Turn off leds");
              }
            } 
          // Else
          } else {
            multiDigitalWrite(0x0);
          }
          // End this thread
          runned();
        }
        
    };

    DrainThread drainThread;
    LightThread lightThread;

    ActuatorsThreadController() : StaticThreadController(&drainThread, &lightThread) {
      
    };

};


class AutoPilotThreadController : public StaticThreadController<2> {

  // The threads defined here can have arbitrary interval duration due to the fact
  // that they are independent respect to the others thread and their execution is not essential
  // in a short period of time (i.e plants can be watered at different times)

  public:
  
    class AutoPilotDrainThread : public Thread {
      
      public: 
    
        AutoPilotDrainThread() : Thread() { 
          // Repeat this task every minute
          interval = 1000 * 60; 
        }

        void run() {
          if (cfh.a_t.is_on){
            // Define temporary pointer which address the autopilot_t struct
            struct cmd_fmt::autopilot_t::drain_autopilot_t *temp = &cfh.a_t.drain_a_t;
            if (inTime(temp->start_h, temp->start_m, temp->end_h, temp->end_m)) {
              // If dryness is more then required then start irrigation
              if (sensor_data.dryness > cfh.a_t.drain_a_t.dryness_max){
                cfh.d_t.is_on = true;
                // Take a limit of 10 ml as a form of closed loop function
                cfh.d_t.limit = 10;
                cfh.d_t.origin = time(NULL);
                // Log autopilot start working
                Logger::notice("AutoPilotDrainThread/run", "Due to dryness, autopilot decided to start draining");
              }
            } else {
              cfh.d_t.is_on = false;
            }
          }
          runned();
        }
    
    };

    class AutoPilotLightThread : public Thread {

      public:
      
        AutoPilotLightThread() : Thread() { 
          // Repeat this task every minute.
          interval = 1000 * 60;  
        }

        void run() {
          if (cfh.a_t.is_on){
            // Define temporary pointer which address the autopilot_t struct
            struct cmd_fmt::autopilot_t::light_autopilot_t *temp = &cfh.a_t.light_a_t;
            if (inTime(temp->start_h, temp->start_m, temp->end_h, temp->end_m)) {
              // If brightness is less then required then turn on leds
              if (sensor_data.brightness < cfh.a_t.light_a_t.brightness_min){
                // White light
                cfh.l_t.rgb = RED_MASK | GREEN_MASK | BLUE_MASK;
                // Check the brightness of the environment every hour
                cfh.l_t.limit = 1000 * 60 * 60; 
                cfh.l_t.origin = time(NULL);
                // Log autopilot start working
                Logger::notice("AutoPilotLightThread/run", "Due to poor lighting, autopilot has set the LEDs to turn on");
              }
            } else {
              cfh.l_t.rgb = 0x0;
            }
          }
          runned();
        }
        
    };

    AutoPilotDrainThread APDrainThread;
    AutoPilotLightThread APLightThread;
  
    AutoPilotThreadController() : StaticThreadController(&APDrainThread, &APLightThread) {
    
    }
  
};

// Functions 
void on_connected(WiFiEvent_t event, WiFiEventInfo_t info);
void on_disconnected(WiFiEvent_t event, WiFiEventInfo_t info);
void on_ap_staconnected(WiFiEvent_t event, WiFiEventInfo_t info);
void on_ap_stadisconnected(WiFiEvent_t event, WiFiEventInfo_t info);


// Thread controllers
SensorThreadController sensorThreadController;
ConnectionThreadsController connectionThreadsController;
ActuatorsThreadController actuatorsThreadController;
AutoPilotThreadController autoPilotThreadController;


void setup() {

  Serial.begin(115200);

  // Initialize wifi in station mode
  WiFi.mode(WIFI_STA);

  // Setup events handler for wifi
  WiFi.onEvent(on_connected, SYSTEM_EVENT_STA_CONNECTED);
  WiFi.onEvent(on_disconnected, SYSTEM_EVENT_STA_DISCONNECTED);
  WiFi.onEvent(on_ap_staconnected, SYSTEM_EVENT_AP_STACONNECTED);
  WiFi.onEvent(on_ap_stadisconnected, SYSTEM_EVENT_AP_STADISCONNECTED);
  
  // Start wifi
  WiFi.begin();

  configTime(ini.gmtOffset_sec, ini.daylightOffset_sec, ini.ntpServer);

  // Set log level in verbose mode
  Logger::setLogLevel(Logger::VERBOSE);

}


void loop() {
  
  // Read sensors
  sensorThreadController.run();  

  // Running server thread controller
  connectionThreadsController.run();
  
  // Running actuators thread controller
  actuatorsThreadController.run();

  // Running autoPilot AI
  autoPilotThreadController.run();
  
  // Default
  delay(500);
  
}


void on_connected(WiFiEvent_t event, WiFiEventInfo_t info) {  
  // Log connection to WiFi hotspot
  Logger::notice("WiFiEvent/on_connected", "Connected to WiFi hotspot");
  
  // Disconnect from access point mode
  WiFi.softAPdisconnect(true);
  
  connectionThreadsController.clear();
  // Add mqtt connection thread controller
  connectionThreadsController.add(&connectionThreadsController.mqttConn);
}


void on_disconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  // Log disconnection info
  Logger::warning("WiFiEvent/on_disconnected", "Disconnected from WiFi hotspot due to " + info.disconnected.reason);
  // Clear 
  connectionThreadsController.clear();
  
  // Try to reconnect to access point
  WiFi.reconnect();
  
  // Log attempt
  Logger::notice("WiFiEvent/on_disconnected", "New attempt to reconnect to WiFi hotspot");
  
  // Enable access point mode
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP(LOCAL_AP_SSID, LOCAL_AP_PASSWORD, 1, false, 1);
  
  // Log access point enabled
  Logger::verbose("WiFiEvent/on_disconnected", "Access point mode enabled");
}


void on_ap_staconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  // Log device connection to access point
  Logger::notice("WiFiEvent/on_ap_staconnected", "New device connected to access point");
  
  // Add server thread for handle wifi credentials requests
  connectionThreadsController.add(&connectionThreadsController.initConn);
  // Start server
  // NOTE! Don't start server before this event or it will raise an exception
  connectionThreadsController.initConn.startServer();
  
  // Stop arduino from station mode
  WiFi.disconnect();
}


void on_ap_stadisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  // Log device disconnection from access point
  Logger::warning("WiFiEvent/on_ap_stadisconnected", "Device disconnected from access point");
  
  // Disconnect from client
  connectionThreadsController.initConn.stopClient();
  connectionThreadsController.clear();
  
  // Realloc arduino in station mode 
  WiFi.begin(ini.ssid, ini.passwd);
}

