# Ultra easy IoT BME280 based Weather Station
Based on https://lastminuteengineers.com/bme280-esp8266-weather-station. Using Google's Open Sans font (300/600 woff2 files) and a custom crafted jquery library. All bundled together on the data/ directory.

# Requirements
* Any ESP8266 board
* A BME280 sensor
* Visual Studio Code + PlatformIO for vscode
* ESP8266 board support on PlatformIO
* Arduino Framework for ESP8266 installed on PlatformIO

# Hardware Setup
This project is quite simple and doesn't need that much of wiring. Connect the BME280's SCL to ESP's D1 (GPIO5) and SDA to D2 (GPIO4). Provide 3v3 to the BME280 module (you can use the 3v3 output of most ESP8266 modules, since the BME280 doesn't draws that much current). Don't mind about the other 2 pins that some BME280 modules provide.
An important observation about the SDA and SCL lines: Please check if your BME280 board contains pull ups for them (from both SDA and SCL to 3v3/VCC of the module). If not, put a 4.7k/10k pull up for each of those lines to 3v3/VCC.

# Firmware Setup + Build
1. Clone this repo with its submodules:
`git clone --recurse-submodules https://github.com/cocus/esp8266-bme280-weather.git`
2. Open this directory on vscode.
3. Edit the `platformio.ini` file and change the `upload_port` and `monitor_port` to reflect your board serial port (or IP address if it already has an Arduino OTA sketch running). For Windows, it should be `COMx` (where x is an integer; please check which one on the Device Manager); on Linux it generally is `/dev/ttyUSBx` (check which one is it with `dmesg | grep ttyUSB` or `ls -lah /dev/ttyUSB*`).
4. Edit the `main.cpp` file and add your WiFi AP(s) on the top. There's an example there on how to add more items.
5. Click on the PlatformIO icon on the sidebar. Select the option `Upload` which should build + flash the board's firmware via the configured port. After this, select the `Upload File System Image` to upload the SPIFFS image (everything contained on data/ gets converted into an image that needs to be flashed separately from the main firmware).
6. Finally, click on the `Monitor` option to open a serial console. Some messages should appear, including the IP address given by the WiFi AP. Navigate to that IP address with a web browser to read the current status of the weather station.