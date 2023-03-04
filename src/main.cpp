#define LOG_LEVEL LOG_DEBUG
#include "config.h"

RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR time_t lastBootTime = 0;
RTC_DATA_ATTR time_t lastSleepTime = 0;

// broadcast mac address used by lifx client
#define BROADCAST_MAC "000000000000"
// broadcast ip address used by lifx client
#define BROADCAST_IP "192.168.1.255"

// pin number for HC-SR501 output
#define PIN_PIRSENSOR 8
// pin number for HC-SR04 trigger
#define PIN_TRIGGER 4
// pin number for HC-SR04 echo
#define PIN_ECHO 5
// max distance we want to measure (in centimeters).
#define US_MAX_DISTANCE_CM 400
// max distance in cm for ultrasonic presence
#define US_PRESENCE_MAX_CM 120
// timeout for PIR motion sensor
#define PIR_TIMEOUT_SECONDS 90
//  timeout for ultrasonic sensor
#define US_TIMEOUT_SECONDS 10
// the amount of times to probe the ultrasonic sensor.
#define NUM_US_PROBES 5

// conversion factor for micro seconds to seconds
#define uS_TO_S_FACTOR 1000000
// number of seconds to deep sleep
#define DEEP_SLEEP_SECONDS 5
// main loop interval (in milliseconds).
#define LOOP_INTERVAL_MS 1000
NewPing sonar(PIN_TRIGGER, PIN_ECHO, US_MAX_DISTANCE_CM);

time_t pir_presence_time;
time_t us_presence_time;
bool presence_timeout;

void setup() {
    ++bootCount;
    Serial.begin(9600);
    // while (!Serial)
    //     ;

    pinMode(PIN_PIRSENSOR, INPUT);  // declare sensor as input
    pinMode(PIN_TRIGGER, OUTPUT);   // sets the pin as an output
    pinMode(PIN_ECHO, INPUT);       // sets the pin as an input

    // Connect to WiFi
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    logf(LOG_INFO, "connecting to WiFi SSID %s...", WIFI_SSID);

    // Retry until success or give up
    int attempts = 0;
    while (attempts++ <= MAX_RETRIES && WiFi.status() != WL_CONNECTED) {
        delay(1000);
    }

    if (WiFi.status() != WL_CONNECTED) {
        const char* errMsg = "WiFi connect timeout";
        log(LOG_ERROR, errMsg);
        sleep();
    }
    IPAddress myIp = WiFi.localIP();
    // Print the IP address
    logf(LOG_INFO, "IP address: %s", myIp.toString());

    // Set up remote logging with mqtt broker
    logf(LOG_INFO, "configuring remote logging with broker %s...", MQTT_BROKER);
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    attempts = 0;
    while (!mqttClient.connect(MQTT_CLIENT_ID)) {
        if (attempts++ >= MAX_RETRIES) {
            log(LOG_WARNING,
                "failed to connect remote logging, fallback to serial");
            break;
        }
        logf(LOG_WARNING, "mqtt connection failed, rc=%d", mqttClient.state());
        delay(250);
    }

    // Get time from NTP server
    timeClient.begin();
    timeClient.update();
    // Sync RTC with NTP time
    setTime(timeClient.getEpochTime());
    logf(LOG_DEBUG, "time synced to %s", fmtTime(now()));

    time_t bootTime = now();

    logf(LOG_INFO, "boot count: %d", bootCount);
    logf(LOG_INFO, "boot time:          %02d:%02d:%02d %02d/%02d/%d",
         hour(bootTime), minute(bootTime), second(bootTime), day(bootTime),
         month(bootTime), year(bootTime));

    if (lastBootTime > 0) {
        logf(LOG_INFO, "last boot time:    %02d:%02d:%02d %02d/%02d/%d",
             hour(lastBootTime), minute(lastBootTime), second(lastBootTime),
             day(lastBootTime), month(lastBootTime), year(lastBootTime));
    }
    lastBootTime = bootTime;

    if (lastSleepTime > 0) {
        logf(LOG_INFO, "last sleep time: %s", fmtTime(lastSleepTime));
    }

    // init lifx client
    lifx.begin(myIp, BROADCAST_IP, BROADCAST_MAC);
    // for directed packets, tagged = 0; for broadcast, tagged =1:
    lifx.setFlags(1, 1, 1);

    presence_timeout = false;
    time_t nowtime = now();
    pir_presence_time = nowtime;
    us_presence_time = nowtime;
}

void loop() {
    time_t nowtime = now();

    if (is_motion_detected()) {
        presence_timeout = false;
        // reset presence times for sensors
        pir_presence_time = nowtime;
        us_presence_time = nowtime;
        logf(LOG_INFO, "presence detected: PIR");

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
        logf(LOG_INFO, "presence detected: US");
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

bool is_motion_detected() { return digitalRead(PIN_PIRSENSOR) == HIGH; }

bool is_ultrasonic_detected() {
    unsigned long distance_cm = us_get_distance_cm();
    logf(LOG_DEBUG, "US distance: %ldcm", distance_cm);

    return distance_cm > 0 && distance_cm < US_PRESENCE_MAX_CM;
}

unsigned long us_get_distance_cm() { return sonar.ping_cm(); }

void sleep() {
    log(LOG_NOTICE, "deep sleep initiated");
    logf(LOG_DEBUG, "time now is %s", fmtTime(now()));

    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_SECONDS * uS_TO_S_FACTOR);
    logf(LOG_NOTICE, "deep sleeping for %d seconds", DEEP_SLEEP_SECONDS);
    log(LOG_NOTICE, "deep sleeping in 5 seconds...");
    delay(5000);

    WiFi.mode(WIFI_OFF);
    lastSleepTime = now();
    esp_deep_sleep_start();
}