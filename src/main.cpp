#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>

#include <ESP8266WebServer.h>
#include <FS.h>

#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

Adafruit_BME280 bme;

ESP8266WebServer server(80);

ESP8266WiFiMulti wifiMulti;

// Blink intervals for normal operation and when an OTA is running (in ms)
static const unsigned long blink_interval_normal = 1000;
static const unsigned long blink_interval_ota = 150;

// If the user LED (usually blue) is connected to another GPIO, change the number
// here:
static constexpr int BlueLed = 2;

// string that holds the compile time (used as versioning)
static const char compile_date[] = __DATE__ " " __TIME__;

// This defines the list of APs where the ESP8266 should connect to when
// it founds one of them.
#define WIFI_APS(d) d ("SampleWiFiAp", "SampleWiFiPassword"); \
                    d ("AnotherWifi Name could go here", "And the password for that AP");

#define CONSOLE_LOG_NO_NL(fmt, ...) Serial.printf("[%lu] %s(): " fmt, millis(), __FUNCTION__, ## __VA_ARGS__);
#define CONSOLE_LOG(fmt, ...)       CONSOLE_LOG_NO_NL(fmt "\n", ## __VA_ARGS__);

void blinkIfNeeded(unsigned long interval)
{
    static unsigned long blink_previous_time = 0;

    unsigned long diff = millis() - blink_previous_time;

    // Hopefully the diff should not be that far from interval
    // (might be caused by callbacks that take too much time)
    if(diff > interval)
    {
        digitalWrite(BlueLed, !digitalRead(BlueLed));  // Change the state of the LED
        blink_previous_time += diff;
    }
}

String GetContentType(String filename)
{
    if(filename.endsWith(".htm")) return "text/html";
    else if(filename.endsWith(".html")) return "text/html";
    else if(filename.endsWith(".css")) return "text/css";
    else if(filename.endsWith(".js")) return "application/javascript";
    else if(filename.endsWith(".png")) return "image/png";
    else if(filename.endsWith(".gif")) return "image/gif";
    else if(filename.endsWith(".jpg")) return "image/jpeg";
    else if(filename.endsWith(".ico")) return "image/x-icon";
    else if(filename.endsWith(".xml")) return "text/xml";
    else if(filename.endsWith(".pdf")) return "application/x-pdf";
    else if(filename.endsWith(".zip")) return "application/x-zip";
    else if(filename.endsWith(".gz")) return "application/x-gzip";
    else if(filename.endsWith(".json")) return "application/json";
    return "text/plain";
}

void ServeFile(String path)
{
    File file = SPIFFS.open(path, "r");
    server.streamFile(file, GetContentType(path));
    file.close();
}

void ServeFile(String path, String contentType)
{
    File file = SPIFFS.open(path, "r");
    server.streamFile(file, contentType);
    file.close();
}

bool HandleFileRead(String path)
{
    if (path.endsWith("/"))
    {
        path += "index.html";
    }

    if (SPIFFS.exists(path))
    {
        // Log the file name that's being sent and the remote IP
        CONSOLE_LOG(
            "spiffs:/%s (%s)",
            path.c_str(),
            server.client().remoteIP().toString().c_str()
        );
        ServeFile(path);
        return true;
    }

    // Log the file name that wasn't found and the remote IP
    CONSOLE_LOG(
        "%s File Not Found (%s)",
        path.c_str(),
        server.client().remoteIP().toString().c_str()
    );
    return false;
}

void setupWiFi()
{
    WiFi.mode(WIFI_STA);
    WIFI_APS(wifiMulti.addAP);

    CONSOLE_LOG_NO_NL("Connecting to WiFi");

    // Wait while WifiMulti connects to one of the configured APs
    while (wifiMulti.run() != WL_CONNECTED)
    {
        delay(250);
        Serial.print(".");
    }

    Serial.println();
    // Show the AP where it got connected and the IP assigned
    CONSOLE_LOG("connected to %s, IP: %s", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}

void setupOTA()
{
    // No authentication by default
    // ArduinoOTA.setPassword((const char *)"123");
    ArduinoOTA.onStart([]() {
        CONSOLE_LOG("OTA-Start, type = %s", ArduinoOTA.getCommand() == U_FLASH ? "program" : "spiffs");
    });
    ArduinoOTA.onEnd([]() {
        CONSOLE_LOG("OTA-End");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        blinkIfNeeded(blink_interval_ota);
        Serial.printf("OTA-Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        CONSOLE_LOG("OTA-Error[%u]: %s", error, [error] () {
            switch (error)
            {
                case OTA_AUTH_ERROR: return "Auth Failed";
                case OTA_BEGIN_ERROR: return "Begin Failed";
                case OTA_CONNECT_ERROR: return "Connect Failed";
                case OTA_RECEIVE_ERROR: return "Receive Failed";
                case OTA_END_ERROR: return "End Failed";
            }
            return "Unknown";
        }());

    });
    ArduinoOTA.begin();
    CONSOLE_LOG("OTA started");
}

void server_on_getData_json(void)
{
    ssize_t buff_len = 100;
    char buffer[buff_len];
    float temperature, humidity, pressure;

    temperature = bme.readTemperature();
    humidity = bme.readHumidity();
    pressure = bme.readPressure() / 100.0F;

    snprintf(
        buffer,
        buff_len,
        "{\"temp\": \"%f\",\"hum\": \"%f\", \"pressure\": \"%f\"}",
        temperature,
        humidity,
        pressure
    );
    server.send(
        200,
        "application/json",
        buffer
    );
}

void server_on_getNodeInfo_json(void)
{
    ssize_t buff_len = 200;
    char buffer[buff_len];
    unsigned long int millisecs = millis();

    snprintf(
        buffer,
        buff_len,
        "{ \"hostname\" : \"%s\", \"local_ip\" : \"%s\", \"uptime\" : \"%lu\", \"sw_build\" : \"%s\", \"ssid\" : \"%s\", \"signal\" : \"%d\"}",
        WiFi.hostname().c_str(),
        WiFi.localIP().toString().c_str(),
        millisecs,
        compile_date,
        WiFi.SSID().c_str(),
        WiFi.RSSI()
    );
    server.send(
        200,
        "application/json",
        buffer
    );
}

void server_on_NotFound(void)
{
    // If the client requests any URI
    // send it if it exists
    if (!HandleFileRead(server.uri()))
    {
        // otherwise, respond with a 404 (Not Found) error
        server.send(404, "text/plain", "Not found");
    }
}

void setupServer()
{
    // Custom CGI for /getData.json
    server.on("/getData.json", server_on_getData_json);
    // Custom CGI for /getNodeInfo.json
    server.on("/getNodeInfo.json", server_on_getNodeInfo_json);

    server.onNotFound(server_on_NotFound);

    server.begin();
    CONSOLE_LOG("HTTP server started");
}

void setup()
{
    Serial.begin(115200);
    delay(100);
    CONSOLE_LOG("Hi! ESP8266 Weather Station. Build time %s", compile_date);

    pinMode(BlueLed, OUTPUT);
    digitalWrite(BlueLed, 1);

    // Init the BME280
    bme.begin(0x76);

    // Init the SPIFFS
    SPIFFS.begin();

    // Configure and connect to WiFi
    setupWiFi();
    // Configure OTA
    setupOTA();
    // Configure the HTTP server
    setupServer();
}

void loop()
{
    // Check if there's something to do regarding the OTA
    ArduinoOTA.handle();
    // Check if there's something to do regarding the HTTP server
    server.handleClient();
    // Blink the LED at the normal rate
    blinkIfNeeded(blink_interval_normal);
}
