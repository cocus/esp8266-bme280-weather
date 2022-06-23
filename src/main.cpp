#include <ESP8266WiFi.h>
//#include <ESP8266WiFiMulti.h>

#include <ESP8266WebServer.h>
#include <LittleFS.h>

#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager

WiFiManager wm; // global wm instance
WiFiManagerParameter custom_field("otherNodeIP", "Other Node IP", "", 32, "placeholder=\"IP here\""); // global param ( for non blocking w params )

Adafruit_BME280 bme;

ESP8266WebServer server(80);

// Blink intervals for normal operation and when an OTA is running (in ms)
static const unsigned long blink_interval_normal = 1000;
static const unsigned long blink_interval_ota = 150;
static const unsigned long blink_interval_ap = 500;

// If the user LED (usually blue) is connected to another GPIO, change the number
// here:
static constexpr int BlueLed = 2;

// string that holds the compile time (used as versioning)
static const char compile_date[] = __DATE__ " " __TIME__;

// Flag that specifies if the weather station http server is running or not
static bool http_server_running = false;

// File that stores the "otherNodeIP" configuration
static const char cfg_otherNodeIP[] = "/otherNodeIP.txt";

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
    File file = LittleFS.open(path, "r");
    server.streamFile(file, GetContentType(path));
    file.close();
}

void ServeFile(String path, String contentType)
{
    File file = LittleFS.open(path, "r");
    server.streamFile(file, contentType);
    file.close();
}

bool HandleFileRead(String path)
{
    if (path.endsWith("/"))
    {
        path += "index.html";
    }

    if (LittleFS.exists(path))
    {
        // Log the file name that's being sent and the remote IP
        CONSOLE_LOG(
            "littlefs:/%s (%s)",
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

/* from https://github.com/tzapu/WiFiManager/blob/master/examples/Parameters/LittleFS/LittleFSParameters.ino */
String readFile(fs::FS &fs, const char *path)
{
    CONSOLE_LOG("Reading file: %s", path);
    File file = fs.open(path, "r");
    if (!file || file.isDirectory())
    {
        CONSOLE_LOG("- empty file or failed to open file");
        return String();
    }
    String fileContent;
    while (file.available())
    {
        fileContent += String((char)file.read());
    }
    file.close();
    CONSOLE_LOG("- read from file: '%s'", fileContent.c_str());
    return fileContent;
}
void writeFile(fs::FS &fs, const char *path, const char *message)
{
    CONSOLE_LOG("Writing file: %s\r\n", path);
    File file = fs.open(path, "w");
    if (!file)
    {
        CONSOLE_LOG("- failed to open file for writing");
        return;
    }
    if (file.print(message))
    {
        CONSOLE_LOG("- file written");
    }
    else
    {
        CONSOLE_LOG("- write failed");
    }
    file.close();
}

String getParam(String name)
{
    //read parameter from server, for customhmtl input
    String value;
    if(wm.server->hasArg(name))
    {
        value = wm.server->arg(name);
    }
    return value;
}

void saveParamCallback()
{
    CONSOLE_LOG("[CALLBACK] saveParamCallback fired");
    CONSOLE_LOG("PARAM otherNodeIP = '%s'", getParam("otherNodeIP").c_str());
    writeFile(LittleFS, cfg_otherNodeIP, getParam("otherNodeIP").c_str());
}

void configModeCallback(WiFiManager *myWiFiManager)
{
    CONSOLE_LOG("Entered config mode, IP = %s, SSID = %s",
        WiFi.softAPIP().toString().c_str(),
        myWiFiManager->getConfigPortalSSID().c_str());

    CONSOLE_LOG("HTTP: stop()");
    http_server_running = false;
    server.stop();
}

String getCustomAPName()
{
    String hostString = String(WIFI_getChipId(),HEX);
    hostString.toUpperCase();
    return "BME280-Weather-" + hostString;
}

void setupWiFi()
{
    WiFi.mode(WIFI_STA);

    wm.setConfigPortalBlocking(false);
    wm.setAPCallback(configModeCallback);

    wm.addParameter(&custom_field);
    wm.setSaveParamsCallback(saveParamCallback);

    // custom menu via array or vector
    std::vector<const char *> menu = {"wifi","info","param","sep","restart","exit"};
    wm.setMenu(menu);

    // set dark theme
    wm.setClass("invert");

    //set static ip
    // wm.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0)); // set static ip,gw,sn
    // wm.setShowStaticFields(true); // force show static ip fields
    // wm.setShowDnsFields(true);    // force show dns field always

    // wm.setConnectTimeout(20); // how long to try to connect for before continuing
    wm.setConfigPortalTimeout(30); // auto close configportal after n seconds
    // wm.setCaptivePortalEnable(false); // disable captive portal redirection
    wm.setAPClientCheck(true); // avoid timeout if client connected to softap

    // wifi scan settings
    // wm.setRemoveDuplicateAPs(false); // do not remove duplicate ap names (true)
    // wm.setMinimumSignalQuality(20);  // set min RSSI (percentage) to show in scans, null = 8%
    // wm.setShowInfoErase(false);      // do not show erase button on info page
    // wm.setScanDispPerc(true);       // show RSSI as percentage not graph icons

    // wm.setBreakAfterConfig(true);   // always exit configportal even if wifi save fails

    wm.autoConnect(getCustomAPName().c_str()); // anonymous ap
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
        "{\"temp\": \"%f\", \"hum\": \"%f\", \"pressure\": \"%f\"}",
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
        "{ \"hostname\" : \"%s\", \"local_ip\" : \"%s\", \"uptime\" : \"%lu\", \"sw_build\" : \"%s\", \"ssid\" : \"%s\", \"signal\" : \"%d\", \"other_ip\": \"%s\"}",
        WiFi.hostname().c_str(),
        WiFi.localIP().toString().c_str(),
        millisecs,
        compile_date,
        WiFi.SSID().c_str(),
        WiFi.RSSI(),
        LittleFS.exists(cfg_otherNodeIP) ? readFile(LittleFS, cfg_otherNodeIP).c_str() : ""
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

void server_on_reset_Cfg(void)
{
    CONSOLE_LOG("Resetting WiFi Manager settings and custom parameters!");
    wm.resetSettings();
    if (LittleFS.exists(cfg_otherNodeIP))
    {
        LittleFS.remove(cfg_otherNodeIP);
    }

    server.send(200, "application/json", "{}");

    ESP.restart();
}

void setupServer()
{
    // Custom CGI for /getData.json
    server.on("/getData.json", server_on_getData_json);
    // Custom CGI for /getNodeInfo.json
    server.on("/getNodeInfo.json", server_on_getNodeInfo_json);
    // Custom handler for /resetCfg
    server.on("/resetCfg", server_on_reset_Cfg);

    server.onNotFound(server_on_NotFound);

    // Enable cross-site requests
    server.enableCORS(true);

    CONSOLE_LOG("HTTP server configured");
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

    // Init LittleFS
    LittleFS.begin();

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
    // Give time to the WiFi Manager
    wm.process();
    if (WiFi.status() == WL_CONNECTED)
    {
        // Blink the LED at the normal rate
        blinkIfNeeded(blink_interval_normal);
        if (!http_server_running)
        {
            CONSOLE_LOG("HTTP: begin()");
            server.begin();
            http_server_running = true;
        }
    }
    else
    {
        // Blink the LED at the AP rate
        blinkIfNeeded(blink_interval_ap);
    }
}
