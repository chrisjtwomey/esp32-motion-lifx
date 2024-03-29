#include "lib.h"

// The number of times we have booted (from off or from sleep).
RTC_DATA_ATTR int bootCount = 0;

// remote mqtt logger
WiFiClient espClient;
PubSubClient client(espClient);
MqttLogger mqttLogger(client, "", MqttLoggerMode::SerialOnly);
// queue to store messages to publish once mqtt connection is established.
cppQueue logQ(sizeof(char) * 100, LOG_QUEUE_MAX_ENTRIES, FIFO, true);
// lifx manager
WiFiUDP udp;
ArduinoLifx lifx(udp);
// init ultrasonic sensor
NewPing sonar(PIN_TRIGGER, PIN_ECHO, US_MAX_DISTANCE_CM);

/**
  Returns whether the PIR sensor detects motion.

  @returns boolean whether the PIN_PIRSENSOR is set to HIGH.
*/
bool is_motion_detected() { return digitalRead(PIN_PIRSENSOR) == HIGH; }

/**
  Returns whether the ultrasonic sensor detects an object in front.

  @returns boolean whether the ultraonic sensor detects an object closer than
  US_MAX_DISTANCE_CM.
*/
bool is_ultrasonic_detected() {
    unsigned long distance_cm = us_get_distance_cm();
    logf(LOG_DEBUG, "US distance: %ldcm", distance_cm);

    return distance_cm > 0 && distance_cm < US_MAX_DISTANCE_CM;
}

/**
  Returns the distance from the sensor to the closest object in front.

  @returns in centimetres the distance from the sensor to the closest object in
  front.
*/
unsigned long us_get_distance_cm() { return sonar.ping_cm(); }

/**
  Connect to a WiFi network in Station Mode.

  @param ssid the network SSID.
  @param pass the network password.
  @param max_attempts the number of connection attempts to make before returning
  an error.
  @returns the esp_err_t code:
  - ESP_OK if successful.
  - ESP_ERR_TIMEOUT if number of retries is exceeded without success.
*/
esp_err_t configureWiFi(const char* ssid, const char* pass, int max_attempts) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    logf(LOG_INFO, "connecting to WiFi SSID %s...", ssid);

    // Retry until success or give up
    int attempts = 0;
    while (attempts++ <= max_attempts && WiFi.status() != WL_CONNECTED) {
        logf(LOG_DEBUG, "connection attempt #%d...", attempts);
        delay(1000);
    }

    // If still not connected, error with timeout.
    if (WiFi.status() != WL_CONNECTED) {
        return ESP_ERR_TIMEOUT;
    }
    // Print the IP address
    logf(LOG_INFO, "IP address: %s", WiFi.localIP().toString());

    return ESP_OK;
}

/**
  Connect to a MQTT broker for remote logging.

  @param broker the hostname of the MQTT broker.
  @param port the port of the MQTT broker.
  @param topic the topic to publish logs to.
  @param clientID the name of the logger client to appear as.
  @param max_attempts the number of connection attempts to make before fallback
  to serial-only logging.
  @returns the esp_err_t code:
  - ESP_OK if successful.
  - ESP_ERR_TIMEOUT if number of retries is exceeded without success.
*/
esp_err_t configureMQTT(const char* broker, int port, const char* topic,
                        const char* clientID, int max_attempts) {
    log(LOG_INFO, "configuring remote MQTT logging...");

    client.setServer(broker, port);
    // Attempt to connect to MQTT broker.
    int attempts = 0;
    while (attempts++ <= max_attempts && !client.connect(clientID)) {
        logf(LOG_DEBUG, "connection attempt #%d...", attempts);
        delay(250);
    }

    if (!client.connected()) {
        return ESP_ERR_TIMEOUT;
    }

    mqttLogger.setTopic(topic);
    mqttLogger.setMode(MqttLoggerMode::MqttAndSerial);

    // Print the IP address
    logf(LOG_INFO, "connected to MQTT broker %s:%d", broker, port);

    return ESP_OK;
}

/**
  Converts a priority into a log level prefix.

  @param pri the log level / priority of the message, see LOG_LEVEL.
  @returns the string value of the priority.
*/
const char* msgPrefix(uint16_t pri) {
    char* priority;

    switch (pri) {
        case LOG_CRIT:
            priority = (char*)"CRITICAL";
            break;
        case LOG_ERROR:
            priority = (char*)"ERROR";
            break;
        case LOG_WARNING:
            priority = (char*)"WARNING";
            break;
        case LOG_NOTICE:
            priority = (char*)"NOTICE";
            break;
        case LOG_INFO:
            priority = (char*)"INFO";
            break;
        case LOG_DEBUG:
            priority = (char*)"DEBUG";
            break;
        default:
            priority = (char*)"INFO";
            break;
    }

    char* prefix = new char[35];
    sprintf(prefix, "%s - ", priority);
    return prefix;
}

/**
  Log a message.

  @param pri the log level / priority of the message, see LOG_LEVEL.
  @param msg the message to log.
*/
void log(uint16_t pri, const char* msg) {
    if (pri > LOG_LEVEL) return;

    const char* prefix = msgPrefix(pri);
    size_t prefixLen = strlen(prefix);
    size_t msgLen = strlen(msg);
    char buf[prefixLen + msgLen + 1];
    strcpy(buf, prefix);
    strcat(buf, msg);
    ensureQueue(buf);
}

/**
  Log a message with formatting.

  @param pri the log level / priority of the message, see LOG_LEVEL.
  @param fmt the format of the log message
*/
void logf(uint16_t pri, const char* fmt, ...) {
    if (pri > LOG_LEVEL) return;

    const char* prefix = msgPrefix(pri);
    size_t prefixLen = strlen(prefix);
    size_t msgLen = strlen(fmt);
    char a[prefixLen + msgLen + 1];
    strcpy(a, prefix);
    strcat(a, fmt);

    va_list args;
    va_start(args, fmt);
    size_t size = snprintf(NULL, 0, a, args);
    char b[size + 1];
    vsprintf(b, a, args);
    ensureQueue(b);
    va_end(args);
}

/**
  Ensure log queue is populated/emptied based on MQTT connection.

  @param msg the log message.
*/
void ensureQueue(char* logMsg) {
    if (!client.connected() || !client.connect(MQTT_CLIENT_ID)) {
        // populate log queue while no mqtt connection
        logQ.push(logMsg);
    } else {
        // send queued logs once we are connected.
        if (logQ.getCount() > 0) {
            mqttLogger.setMode(MqttLoggerMode::MqttOnly);
            while (!logQ.isEmpty()) {
                logQ.pop(logMsg);
                mqttLogger.println(logMsg);
            }
            mqttLogger.setMode(MqttLoggerMode::MqttAndSerial);
        }
    }
    // print/send the current log
    mqttLogger.println(logMsg);
}

/**
  The seconds since awake.

  @returns the seconds since awake.
*/
int now() {
    return millis() / 1000;
}

/**
  Enter deep sleep.

  @param seconds the number of seconds to sleep.
*/
void sleep(int seconds) {
    log(LOG_NOTICE, "deep sleep initiated");

    esp_sleep_enable_timer_wakeup(S_TO_uS(seconds));
    logf(LOG_NOTICE, "deep sleeping for %d seconds", seconds);

    WiFi.mode(WIFI_OFF);
    esp_deep_sleep_start();
}