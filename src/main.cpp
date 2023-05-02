#define LOG_LEVEL LOG_INFO
#define WIFI_SSID "XXXX"
#define WIFI_PASS "XXXX"
#include "lib.h"

int pir_presence_time;
int us_presence_time;
bool presence_timeout;

void setup() {
    ++bootCount;
    Serial.begin(9600);
    // while (!Serial)
    //     ;

    int bootTime = now();
    logf(LOG_INFO, "boot count: %d", bootCount);

    pinMode(PIN_PIRSENSOR, INPUT);  // declare sensor as input
    pinMode(PIN_TRIGGER, OUTPUT);   // sets the pin as an output
    pinMode(PIN_ECHO, INPUT);       // sets the pin as an input

    esp_err_t err;
    err = configureWiFi(WIFI_SSID, WIFI_PASS, 6);
    if (err != ESP_OK) {
        logf(LOG_ERROR, "failed to connect to wifi ssid %s", WIFI_SSID);
        sleep(2);
    }
    IPAddress myIp = WiFi.localIP();

    err = configureMQTT(MQTT_BROKER, MQTT_PORT, MQTT_TOPIC, MQTT_CLIENT_ID, 6);
    if (err != ESP_OK) {
        logf(LOG_ERROR, "failed to connect to mqtt broker %s", MQTT_BROKER);
        sleep(2);
    }

    // init lifx client
    lifx.begin(myIp, BROADCAST_IP, BROADCAST_MAC);
    // for directed packets, tagged = 0; for broadcast, tagged =1:
    lifx.setFlags(1, 1, 1);

    presence_timeout = false;
    int nowtime = now();
    pir_presence_time = nowtime;
    us_presence_time = nowtime;
}

void loop() {
    if (!WiFi.isConnected()) {
        esp_err_t err;
        logf(LOG_WARNING, "WiFi connection lost, attempting to reconnect...");
        err = configureWiFi(WIFI_SSID, WIFI_PASS, 6);
        if (err != ESP_OK) {
            logf(LOG_ERROR, "failed to connect to wifi ssid %s", WIFI_SSID);
            sleep(1);
        }

        err = configureMQTT(MQTT_BROKER, MQTT_PORT, MQTT_TOPIC, MQTT_CLIENT_ID, 6);
        if (err != ESP_OK) {
            logf(LOG_ERROR, "failed to connect to mqtt broker %s", MQTT_BROKER);
            sleep(2);
        }
    }

    int nowtime = now();
    if (is_motion_detected()) {
        presence_timeout = false;
        // reset presence times for sensors
        pir_presence_time = nowtime;
        us_presence_time = nowtime;
        logf(LOG_DEBUG, "presence detected: PIR");

        lifx.setPower(1, 0);
        delay(LOOP_INTERVAL_MS);
        return;
    }

    int secs_since_pir_presence = ceil(nowtime - pir_presence_time);
    if (secs_since_pir_presence < PIR_TIMEOUT_SECONDS) {
        // don't start polling ultrasonic sensor until PIR timeout
        delay(LOOP_INTERVAL_MS);
        return;
    }

    if (presence_timeout) {
        // don't continue polling ultrasonic sensor after US timeout
        delay(LOOP_INTERVAL_MS);
        return;
    }

    if (is_ultrasonic_detected()) {
        presence_timeout = false;
        // reset presence times for sensors
        pir_presence_time = nowtime;
        us_presence_time = nowtime;
        logf(LOG_DEBUG, "presence detected: US");
    }

    int secs_since_us_presence = ceil(nowtime - us_presence_time);
    if (secs_since_us_presence < PIR_TIMEOUT_SECONDS + US_TIMEOUT_SECONDS) {
        // continue polling ultrasonic sensor until timeout
        delay(LOOP_INTERVAL_MS);
        return;
    }

    presence_timeout = true;
    logf(LOG_INFO, "presence timeout");
    lifx.setPower(0, 0);

    // wait until the next check
    delay(LOOP_INTERVAL_MS);
    return;
}