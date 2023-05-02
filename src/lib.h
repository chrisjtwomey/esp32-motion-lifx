#ifndef LIB_H
#define LIB_H
#include <MqttLogger.h>
#include <ArduinoLifx.h>
#include <WiFi.h>
#include <time.h>
#include <WiFiUdp.h>
#include <cppQueue.h>
#include <NewPing.h>

// pin number for HC-SR501 output
#define PIN_PIRSENSOR 8
// pin number for HC-SR04 trigger
#define PIN_TRIGGER 4
// pin number for HC-SR04 echo
#define PIN_ECHO 5
// max distance we want to measure (in centimeters).
#define US_MAX_DISTANCE_CM 150
// timeout for PIR motion sensor
#define PIR_TIMEOUT_SECONDS 90
//  timeout for ultrasonic sensor
#define US_TIMEOUT_SECONDS 10
// main loop interval (in milliseconds).
#define LOOP_INTERVAL_MS 1000

#ifndef MQTT_BROKER
#define MQTT_BROKER "localhost"
#endif
#ifndef MQTT_PORT
#define MQTT_PORT 1883
#endif
#ifndef MQTT_CLIENT_ID
#define MQTT_CLIENT_ID "esp32-lifx-motion"
#endif
#define MQTT_TOPIC "mqtt/esp32-lifx-motion"
// log message entry history size
#define LOG_QUEUE_MAX_ENTRIES 10
// broadcast mac address used by lifx client
#define BROADCAST_MAC "000000000000"
// broadcast ip address used by lifx client
#define BROADCAST_IP "192.168.1.255"

// convert milliseconds to seconds
#define mS_TO_S(S) (S / 1000)
// convert seconds to microseconds
#define S_TO_uS(S) (S * 1000000)

// Enum of log verbosity levels.
#define LOG_CRIT 0
#define LOG_ERROR 1
#define LOG_WARNING 2
#define LOG_NOTICE 3
#define LOG_INFO 4
#define LOG_DEBUG 5

#ifndef LOG_LEVEL
// Debug logging by default.
#define LOG_LEVEL LOG_DEBUG
#endif

// The number of times we have booted (from off or from sleep).
extern RTC_DATA_ATTR int bootCount;
// The remote logging instance.
extern MqttLogger mqttLogger;
// The log message queue.
extern cppQueue logQ;
// The Lifx manager.
extern ArduinoLifx lifx;

/**
  Returns whether the PIR sensor detects motion.

  @returns boolean whether the PIN_PIRSENSOR is set to HIGH.
*/
bool is_motion_detected();

/**
  Returns whether the ultrasonic sensor detects an object in front.

  @returns boolean whether the ultraonic sensor detects an object closer than
  US_PRESENCE_MAX_CM.
*/
bool is_ultrasonic_detected();

/**
  Returns the distance from the sensor to the closest object in front.

  @returns in centimetres the distance from the sensor to the closest object in
  front.
*/
unsigned long us_get_distance_cm();

/**
  Connect to a WiFi network in Station Mode.

  @param ssid the network SSID.
  @param pass the network password.
  @param retries the number of connection attempts to make before returning an
  error.
  @returns the esp_err_t code:
  - ESP_OK if successful.
  - ESP_ERR_TIMEOUT if number of retries is exceeded without success.
*/
esp_err_t configureWiFi(const char* ssid, const char* pass, int retries);

/**
  Connect to a MQTT broker for remote logging.

  @param broker the hostname of the MQTT broker.
  @param port the port of the MQTT broker.
  @param topic the topic to publish logs to.
  @param clientID the name of the logger client to appear as.
  @param max_retries the number of connection attempts to make before fallback
  to serial-only logging.
  @returns the esp_err_t code:
  - ESP_OK if successful.
  - ESP_ERR_TIMEOUT if number of retries is exceeded without success.
*/
esp_err_t configureMQTT(const char* broker, int port, const char* topic,
                        const char* clientID, int max_retries);

/**
  Log a message.

  @param pri the log level / priority of the message, see LOG_LEVEL.
  @param msg the message to log.
*/
void log(uint16_t pri, const char* msg);

/**
  Log a message with formatting.

  @param pri the log level / priority of the message, see LOG_LEVEL.
  @param fmt the format of the log message
*/
void logf(uint16_t pri, const char* fmt, ...);

/**
  Converts a priority into a log level prefix.

  @param pri the log level / priority of the message, see LOG_LEVEL.
  @returns the string value of the priority.
*/
const char* msgPrefix(uint16_t pri);

/**
  Ensure log queue is populated/emptied based on MQTT connection.

  @param msg the log message
*/
void ensureQueue(char* msg);

/**
  The seconds since awake.

  @returns the seconds since awake.
*/
int now();

/**
  Enter deep sleep.

  @param seconds the number of seconds to sleep.
*/
void sleep(int seconds);

#endif