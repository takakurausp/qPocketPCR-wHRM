// qPocketPCR Software
// by Urs Gaudenz GaudiLabs 2025

// Included Libraries
#include "USB.h"
#include "USBMSC.h"
#include "FS.h"
#include "SPIFFS.h"
#include "USB_DRIVE.h"
#include "Parsing.h"

// WiFi / Web Server 有効化フラグ
//   デフォルト = 1 （公開ソースでは現状どおり WiFi AP + Web サーバーが有効）
//   日本の電波法の認証を受けていないため、日本国内で使用するには
//   ビルド時に -DWIFI_ENABLED=0 を指定し WiFi を無効にして書き込んでください。
//   (PlatformIO: platformio.ini の build_flags に "-DWIFI_ENABLED=0" を追加)
#ifndef WIFI_ENABLED
#define WIFI_ENABLED 1
#endif

#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>

#define sensor_t camera_sensor_t
#include "esp_camera.h"
#undef sensor_t

#include "Displays.h"
#include "Keyboard.h"
#include "TLC59108.h"

#include <Adafruit_TLA202x.h>

#include <TFT_eSPI.h> // Display specific library
#include "Free_Fonts.h"
#include "GaudiSans7pt7b.h"
#include "neuropol10pt7b.h"


#include <SPI.h>
#include <Adafruit_FT6206.h>

#include "Buttons.h"
#include "Preferences.h"

#include "esp_task_wdt.h"

#include <SPIFFS.h>


// ==================== CONSTANTS AND CONFIGURATION ====================

// Version and system configuration
#define VERSION_STRING  "v0.24"
#define FORMAT_SPIFFS_IF_FAILED true
#define FILESYSTEM SPIFFS


// Hardware pin definitions

#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      7
#define SIOD_GPIO_NUM     17
#define SIOC_GPIO_NUM     16

#define Y9_GPIO_NUM       41
#define Y8_GPIO_NUM       39
#define Y7_GPIO_NUM       40
#define Y6_GPIO_NUM       5
#define Y5_GPIO_NUM       2
#define Y4_GPIO_NUM       4
#define Y3_GPIO_NUM       3
#define Y2_GPIO_NUM       1
#define VSYNC_GPIO_NUM    9
#define HREF_GPIO_NUM     8
#define PCLK_GPIO_NUM     6

#define HEATER_PWM_PIN  21
#define LID_PWM_PIN     42
#define FAN_PWM_PIN     33
#define TFT_BACKLIGHT  18

// PWM channels
#define FAN_CHANNEL 7  // Channel 0 and 1 used by camera?
#define HEATER_CHANNEL 6
#define LID_CHANNEL 5

// ADC channels
#define ADC_BlockTemp 0
#define ADC_LidTemp 1
#define ADC_VBus 2
#define ADC_VADC 3

// SDA/SCL
#define TLC59108_ADDR  0x40       // Address of TLC59108 (default is 0x40)
#define SDA_PIN  17               // SDA pin 
#define SCL_PIN  16               // SCL pin 
#define TLC59108_HWRESET -1       //  TLC59108 reset 


// PCR parameters
#define PIDp 0.4       //0.42
#define PIDi 0.000    //0.004 // 0.001
#define PIDd 0.02     //0.02//0.1
#define TEMPiMax 1500


#define PIDLIDp 0.4       //0.42 0.15 0.18
#define PIDLIDi 0.002    //0.004 // 0.001
#define PIDLIDiRange 10     
#define TEMPLIDiMax 1500

// System parameters
#define ILLUMINATION_TIME 850  //650 min

#define LidTemp 110 //110

#define CurrentMax 1.7
#define LidCurrentMax 1.3
#define BlockCurrentMax 2.1
#define FanCurrentMax 0.3

#define pwmFreqFAN  6000  // PWM frequency in Hz (above audible range)
#define pwmFreqHEATER  1000  // PWM frequency in Hz 
#define pwmFreqLID  1000  // PWM frequency in Hz 

#define pwmResolution 8  // 8-bit resolution (values from 0 to 255)

#define SENS_WIDTH  640
#define SENS_HEIGHT  120

#define MASK_THRESHOLD 40

#define MAX_MEASUREMENTS 512

#define SAFETY_MIN_TEMP 5
#define SAFETY_MAX_TEMP 130


// ==================== ENUMS AND STRUCTURES ====================


enum ErrorCode {
    ERROR_NONE = 0,
    ERROR_TEMP_SENSOR = 1,
    ERROR_WATCHDOG = 2,
};

enum PCRState {
PCR_HEATLID,
PCR_SET,
PCR_TRANSITION,
PCR_TIME,
PCR_END
};

static const uint16_t my_palette[] PROGMEM = {

  TFT_RED,      //  0
  TFT_ORANGE,   //  1
  TFT_BROWN,    //  2
  TFT_DARKGREEN,    //  3
  TFT_BLUE,     //  4
  TFT_PURPLE,   //  5
  TFT_CYAN,     //  6
  TFT_MAGENTA,  //  7
  TFT_WHITE,    //  9
  //TFT_MAROON,   // 12  Darker red colour
  //TFT_DARKGREEN,// 13  Darker green colour
  //TFT_NAVY,     // 14  Darker blue colour
  //TFT_PINK      // 15
};


// ==================== Constants ====================


const int NTC_LID_B = 3435;
const float NTC_TN = 298.15;
const int NTC_RN = 10000;
const float NTC_R0 = 4.7;

float NTC_A = 1.1235e-3;
float NTC_B = 2.3510e-4;
float NTC_C = 8.3301e-8;
float logR;

const int SENS_OFFSET_X = 100; //46
const int SENS_OFFSET_Y = 240; //234 210
const float SENS_SPACING = 65; //73

const float TEMP_TOLLERANCE = 0.5;

const int NUM_SENSORS = 8;

const int MEASUREMENT_INTERVAL_MS = 200;
const int TEMP_CHECK_INTERVAL_MS = 1000;
const int WD_TIMEOUT_S = 5;


// ==================== Variables ====================


ErrorCode errorCode=ERROR_NONE;

int x_pos = 0;

String myFileName="/DATA.TXT";

int sensorValue = 0;        // value read from the sesnor
int outputValue = 0;        // value output to the PWM (heater)
float temperature = 0;
float temperature_lid = 0;

float temperature_mean = 0;
float temperature_lid_mean = 0;


float sensorResistance = 0;
float sensorVoltage = 0;

int PCRpwm = 0;

float TEMPset;
float TEMPdif;
float TEMPi;
float TEMPLIDi;

bool PIDIntegration = false;
float TEMPcontrol;
float TEMPLIDcontrol;
float TEMPdif_a[10];
float TEMPd;
long TEMPclick = 0;
long TIMEclick = 0;
int TIMEcontrol = 0;

float TEMPLIDset;
float TEMPLIDdif;

int ledBrightness = 225; //3.3k Resistor

float fluorescence[NUM_SENSORS][MAX_MEASUREMENTS];
float wellFactor[NUM_SENSORS];

float last_values[NUM_SENSORS];

// --- Robustness counters (camera capture + data write) ---
int cameraFailCount = 0;   // number of failed camera captures in the current run
int writeFailCount = 0;    // number of failed SPIFFS data-appends in the current run
long sensor_center_x[NUM_SENSORS];
long sensor_center_y[NUM_SENSORS];

int captures=0;
int measurements=0;
float measureTemp = 0; // block temperature at the moment of each measurement (logged to CSV)

unsigned long myTime_measure;
unsigned long myTime_WD;

int measure_interval = 0;


float PCR_Temperatures[5];
int PCR_TIMEs[5];
int PCR_Cycles;

PCRState casePCR = PCR_HEATLID;
int MenuItem = 1;
int PCRstep = 0;
int PCRcycle = 1;

boolean saveReady=false;
boolean cameraOn=false;

boolean touch=false;
boolean saveWellFactors=false;

TS_Point tpoint;

int i;

esp_err_t err=0;

Preferences preferences; // Initialize Preference Manager


TFT_eSPI tft = TFT_eSPI();  // Invoke display library with default width and height
TFT_eSprite camSprite = TFT_eSprite(&tft); // Define Cam Sprite
TFT_eSprite tftbuff = TFT_eSprite(&tft);  // Sprite class


Adafruit_FT6206 ts = Adafruit_FT6206(); // Invoke Touch library
TS_Point p;   //Touch point

Adafruit_TLA202x  tla; // Invoke Analog to Digital Converter

TLC59108 leds(&Wire, TLC59108_ADDR); // Define TLC59108 object for LED Driver using I2C pointer and slave address

// 変更後
camera_sensor_t * cam_sens; // Camera handler
esp_err_t res = ESP_OK; //Camera error handler

uint16_t *camSpriteBuf ;
uint8_t *baseBuf;
boolean *maskBuf;

// WiFi / Web Server
WebServer server(80);
const char* ap_ssid = "qPocketPCR";
const char* ap_password = "12345678";
bool wifiEnabled = false;

// Current startup mode. 0 = disabled (WIFI_ENABLED=0), 1 = access point, 2 = client.
int wifiMode = 0;
// IP address string for the active WiFi interface (client or AP). Empty when disabled.
String wifiIP = "";
bool stopRequested = false;


// ==================== SETUP ====================

// Fetch the current UTC time from an NTP server. Only used when running in WiFi
// client mode (g_deviceEpoch stays 0 otherwise, so FAT timestamps keep their
// template defaults). Returns true on success and stores seconds-since-epoch in
// g_deviceEpoch. Uses a single UDP packet to pool.ntp.org with no external deps.
static bool fetchNtpTime()
{
  const char* ntpHost = "pool.ntp.org";
  const uint16_t ntpPort = 123;
  const uint32_t pollIntervalSec = 3600;

  // NTP timestamp: 64-bit fixed-point seconds since 1900-01-01.
  struct udp_packet {
    uint8_t  lp[48];
  } packet;

  memset(&packet, 0, sizeof(packet));
  packet.lp[0] = 0x1B; // mode 3 (client), version 3

  const uint32_t ntpEpochOffset = 2208988800UL; // 1970-01-01 minus 1900-01-01
  struct timeval tvNow;
  gettimeofday(&tvNow, NULL);
  uint32_t secondsSince1900 = (uint32_t)tvNow.tv_sec + ntpEpochOffset;
  memcpy(&packet.lp[40], &secondsSince1900, 4);

  WiFiUDP udp;
  if (udp.begin(ntpPort) != 1) { Serial.println("NTP: UDP begin failed"); return false; }

  uint32_t startMillis = millis();
  bool gotReply = false;
  while (millis() - startMillis < 4000) {
    esp_task_wdt_reset();
    if (udp.beginPacket(ntpHost, ntpPort)) {
      udp.write(packet.lp, sizeof(packet));
      int err = udp.endPacket();
      if (err == 1) { gotReply = true; break; }
    }
    delay(50);
  }
  if (!gotReply) { Serial.println("NTP: no reply"); udp.stop(); return false; }

  startMillis = millis();
  while (millis() - startMillis < 4000) {
    esp_task_wdt_reset();
    int len = udp.parsePacket();
    if (len >= 48) {
      uint8_t buf[48];
      udp.read(buf, sizeof(buf));
      udp.stop();

      // Verify server reply: version and mode fields in byte 0.
      uint8_t vm = buf[0];
      if ((vm & 0xE0) != 0x00 || (buf[0] & 0x07) == 0) {
        Serial.println("NTP: bad version/mode"); return false;
      }

      // Transmit timestamp is at bytes 40..43.
      uint32_t txSeconds = ((uint32_t)buf[40] << 24) | ((uint32_t)buf[41] << 16)
                         | ((uint32_t)buf[42] << 8)  | (uint32_t)buf[43];
      if (txSeconds == 0) { Serial.println("NTP: zero tx time"); return false; }

      // Estimate local clock offset from round-trip time.
      struct timeval rxNow;
      gettimeofday(&rxNow, NULL);
      uint32_t rxSince1900 = (uint32_t)rxNow.tv_sec + ntpEpochOffset;

      // RTT: (rx - tx) sent by server + (now - rxSent) locally. We approximate
      // using the server's (rx-tx) plus a small local correction.
      uint32_t rtt = (rxSince1900 - txSeconds); // rough, positive when clock ahead
      int32_t offsetSec = 0;
      if (rtt > 0 && rtt < 600) {
        // server reported (rx - tx); local elapsed since send is small -> use half.
        uint32_t localElapsed = (uint32_t)(rxNow.tv_sec - tvNow.tv_sec);
        offsetSec = (int32_t)((rxSince1900 - txSeconds) / 2) - localElapsed;
      }

      time_t epoch = (time_t)txSeconds - ntpEpochOffset + offsetSec;
      if (epoch > 0) {
        g_deviceEpoch = epoch;
        struct tm tmbuf;
        struct tm* ptm = gmtime_r(&epoch, &tmbuf);
        if (ptm) {
          Serial.printf("NTP synced: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                        ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday,
                        ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
        }
        return true;
      }
    }
    delay(50);
  }
  Serial.println("NTP: timeout waiting for reply");
  udp.stop();
  return false;
}


// --- WiFi handler forward declarations (for Arduino IDE top-down compilation) ---
void handleRoot();
void handleBuilder();
void handleStatus();
void handleStart();
void handleStop();
void handleDownload();
void handleUpload();
void handleUploadDone();
void handleListProtocols();
void handleLoadProtocol();
void handleSaveProtocol();
void handleGetProtocol();
void handleSaveActiveProtocol();
void handleDeleteProtocol();


void setup() {

// Initialize WDT with 5 second timeout, reset on trigger
    esp_task_wdt_init(WD_TIMEOUT_S, true);

// Add current task to WDT
    esp_task_wdt_add(NULL);

    
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();


  // Set Init Pins Hardware

  pinMode(TFT_BACKLIGHT, OUTPUT);
  digitalWrite(TFT_BACKLIGHT, HIGH);

  pinMode(HEATER_PWM_PIN, OUTPUT);
  digitalWrite(HEATER_PWM_PIN, LOW);

  pinMode(FAN_PWM_PIN, OUTPUT);
  digitalWrite(FAN_PWM_PIN, LOW);

  pinMode(LID_PWM_PIN, OUTPUT);
  digitalWrite(LID_PWM_PIN, LOW);
  

  // Set the PWM properties
  ledcSetup(FAN_CHANNEL, pwmFreqFAN, pwmResolution);  // Channel 0, Frequency, Resolution
  ledcAttachPin(FAN_PWM_PIN, FAN_CHANNEL);  // Attach the PWM signal to the pin

  ledcSetup(HEATER_CHANNEL, pwmFreqHEATER, pwmResolution);  // Channel 0, Frequency, Resolution
  ledcAttachPin(HEATER_PWM_PIN, HEATER_CHANNEL);  // Attach the PWM signal to the pin

  ledcSetup(LID_CHANNEL, pwmFreqLID, pwmResolution);  // Channel 0, Frequency, Resolution
  ledcAttachPin(LID_PWM_PIN, LID_CHANNEL);  // Attach the PWM signal to the pin


  // Init I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  

// Init TFT and Sprites
// camSprite.setAttribute(PSRAM_ENABLE, false);

  tft.init();
  camSpriteBuf = (uint16_t*)camSprite.createSprite(SENS_WIDTH/2, SENS_HEIGHT/2);
  //baseBuf = (uint16_t*)baseSprite.createSprite(640, 120);

  baseBuf = (uint8_t *)malloc(SENS_WIDTH*SENS_HEIGHT * sizeof(uint8_t));
  maskBuf = (boolean *)malloc(SENS_WIDTH*SENS_HEIGHT * sizeof(boolean));

  tftbuff.createSprite(320, 240-SENS_HEIGHT/2);


// draw screen
  tft.setRotation(1);
  tft.fillScreen(TFT_WHITE);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);

  tft.setFreeFont(&neuropol10pt7b);

  tft.setCursor (20, 30);
  tft.print("Check qPocketPCR");

  tft.setCursor (20, 60);
  tft.setFreeFont(&GaudiSans7pt7b);

status_line("Software",false,"Version "+(String)VERSION_STRING);
status_line("Forked from qPocketPCR V1.1 (GaudiLabs)",false);
status_line("Extended by Takakura",false);


esp_reset_reason_t reason = esp_reset_reason();

if (reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT) {
    status_line("Watchdog reset!",true);
    errorCode=ERROR_WATCHDOG;
    emergencyShutdown();
} else {
    status_line("Normal Startup",false);
}


  status_line("Display Initialized",false);
//  status_line("Hardware Pins Initialized",false);


// Mount SPIFFS drive

  status_line("USB Drive Started",!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED));

// Start USB Driver

  Start_USB_Drive();
  addDataToUSB();
  
  // Init LED driver
  leds.setRegister(TLC59108::REGISTER::ALLCALLADR::ADDR, 0x4d);

  leds.init(TLC59108_HWRESET);
  leds.setLedOutputMode(TLC59108::LED_MODE::PWM_IND);

   SetLEDBrightness(false);
 // status_line("LEDs Initialized",false);


  // Init touscreench

   status_line("Touchscreen started",!ts.begin(40));

   status_line("Sensor test",!tla.begin()); //ADC TLA202x found

  tla.setDataRate(TLA202x_RATE_3300_SPS);
  tla.setRange(TLA202x_RANGE_4_096_V);
  tla.setChannel((tla202x_channel_t)ADC_VBus);
  tla.setMode(TLA202x_MODE_CONTINUOUS);
  delay(10);
  

  status_line("USB-C",(tla.readVoltage()*11<4.5),"Voltage: "+String(tla.readVoltage()*11,2));


 // Start Cam
  err=startCam();
  //status_line("Camera init",err != ESP_OK,"error_code: "+(String)err);

  cam_sens = esp_camera_sensor_get();
  status_line("Camera Sensor Ready",false,"ID: "+(String)cam_sens->id.PID);

// Stop Cam
   err=stopCam();


  // Set preferences

  preferences.begin("qPrefs", false);

  if (preferences.getInt("Initialized", 0) != 1)
  {

  for (int sensor = 0; sensor < NUM_SENSORS; sensor++) {
   wellFactor[sensor] = 1;
   preferences.putFloat("SenFactor"+ sensor, 1.0);
  }

    preferences.putInt("Initialized", 1);
  } else 
  {

  for (int sensor = 0; sensor < NUM_SENSORS; sensor++) {
    sensor_center_x[sensor] = preferences.getInt("SenCentX" + sensor, sensor * SENS_SPACING + SENS_OFFSET_X);
    sensor_center_y[sensor] = preferences.getInt("SenCentY" + sensor, SENS_OFFSET_Y);
    wellFactor[sensor] = preferences.getFloat("SenFactor" + sensor, 1);
    Serial.println(wellFactor[sensor] );
   }
  }

 // status_line("Preferences Initiated",false);


  loadProtocol();
  status_line("Protocol loaded",false);

 if (loadBinFromSPIFFS(baseBuf,SENS_WIDTH*SENS_HEIGHT ,"/base.bin"))
 {
  status_line("Baseline loaded",true);
    for (int i = 0; i < SENS_WIDTH*SENS_HEIGHT; i++) {baseBuf[i]=0;}
 } 
 else   status_line("Baseline loaded",false);

initMask();
if (loadMaskFromSPIFFS((uint8_t *)maskBuf))
 {
  status_line("Mask loaded",true);
    initMask();
 } 
 else   status_line("Mask loaded",false);   

 tft.println(" ");
 tft.println("       Tap to hold...");


  myTime_measure = 0;
  myTime_WD = 0;

  x_pos = 0;

delay(2000);
  while (ts.touched()) 
  {esp_task_wdt_reset();} // Feed the watchdog 
  

  drawMainDisplay();

#if WIFI_ENABLED
  // ---- WiFi: Client mode (if a valid config exists) or Access Point mode ----
  // Read the configuration file from the virtual USB drive. If it does not
  // exist yet, create an empty template so the user has something to edit on
  // the host PC. The file is "WIFI.TXT" with lines:
  //   SSID=<access point name>
  //   PASSWORD=<access point password>
  readWifiConfig();
  createWifiConfigTemplate();

  bool haveCredentials = (wifi_config_ssid[0] != '\0' && wifi_config_password[0] != '\0');

  if (haveCredentials) {
    // Try to connect as a WiFi client to the configured access point.
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifi_config_ssid, wifi_config_password);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
      delay(250);
      esp_task_wdt_reset();
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      wifiEnabled = true;
      wifiMode = 2; // client mode
      String ipStr = WiFi.localIP().toString();
      wifiIP = ipStr;
      Serial.print("WiFi client connected. IP: ");
      Serial.println(ipStr);

      // In client mode we can reach the internet, so sync the RTC via NTP and
      // stamp every file on the virtual USB drive with the current UTC time.
      if (fetchNtpTime()) {
        applyNtpTimestamps();
      } else {
        Serial.println("NTP failed - files keep default timestamps");
      }
    } else {
      // Could not connect - fall back to Access Point mode below.
      Serial.println("WiFi client connection failed, using Access Point mode");
    }
  }

  if (!wifiEnabled) {
    // Access Point mode (default).
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(ap_ssid, ap_password);
    wifiEnabled = true;
    wifiMode = 1; // access point mode
    String ipStr = WiFi.softAPIP().toString();
    wifiIP = ipStr;
    Serial.print("WiFi AP started: ");
    Serial.println(ap_ssid);
    Serial.print("IP: ");
    Serial.println(ipStr);
  }

  // Web サーバーハンドラ登録 (works in both Client and AP mode)
  server.on("/", handleRoot);
  server.on("/builder", handleBuilder);
  server.on("/status", handleStatus);
  server.on("/start", handleStart);
  server.on("/stop", handleStop);
  server.on("/download", handleDownload);
  server.on("/upload", HTTP_POST, handleUploadDone, handleUpload);
  server.on("/listproto", handleListProtocols);
  server.on("/loadproto", handleLoadProtocol);
  server.on("/saveproto", HTTP_POST, handleSaveProtocol);
  server.on("/deleteproto", HTTP_POST, handleDeleteProtocol);
  server.on("/getproto", handleGetProtocol);
  server.on("/saveprotocol", HTTP_POST, handleSaveActiveProtocol);
  server.begin();
  Serial.println("Web server started on port 80");
#else
  // WIFI_ENABLED=0: WiFi/AP/Web サーバーは無効（電波法対応）
  wifiEnabled = false;
  wifiMode = 0; // disabled
  Serial.println("WiFi disabled (WIFI_ENABLED=0)");
#endif

#if WIFI_ENABLED
  // Display the current WiFi startup mode on screen and wait for OK.
  draw_WIFI_display();
  caseUX = CASE_RunWIFI;
#else
  // WIFI_ENABLED=0: no WiFi information is shown; the main display is already drawn.
  caseUX = CASE_Main;
#endif
} // setup

// ==================== MAIN LOOP ====================

void loop() {
  
Service_USB();
if (newConfigAvailable) {loadProtocol();newConfigAvailable=false;if (caseUX==CASE_RunPotocolDisplay) drawProtocolDisplay();}

if (millis() - myTime_WD > TEMP_CHECK_INTERVAL_MS) {readTemperatures();}

// WiFi クライアント処理
if (wifiEnabled) {
  server.handleClient();
}

switch (caseUX) {
  
 case CASE_Main:


      if (ts.touched()) {
        p = ts.getPoint();

        if (PointInRect(p, 0, 0, 218, 128)) // RUN
        {

          tla.setChannel((tla202x_channel_t)ADC_VBus);
          captures=countCaptures();

          if (captures >= MAX_MEASUREMENTS)
          {
            // Too many measurement points for the fluorescence buffer:
            // show a warning and do not enter the run confirmation screen.
            Serial.print("Too many measurements: ");
            Serial.println(captures);
            tft.fillRect(0, 0, 218, 128, TFT_WHITE);
            tft.setTextColor(TFT_RED, TFT_WHITE);
            tft.setFreeFont(&GaudiSans7pt7b);
            tft.setCursor(15, 40);
            tft.print("Protocol too large!");
            tft.setCursor(15, 62);
            tft.print("Measurements: ");
            tft.print(captures);
            tft.setCursor(15, 84);
            tft.print("Max: ");
            tft.print(MAX_MEASUREMENTS - 1);
            tft.setCursor(15, 106);
            tft.print("Split into several runs");
          }
          else
          {
            drawInitRunDisplay(tla.readVoltage()*11);
            caseUX = CASE_InitRun;
          }
        }
        
        if (PointInRect(p, 0, 138, 218, 100)) // PROTOCOL
        {
          setPrototcolDisplayFrame(1);
          drawProtocolDisplay();
          caseUX = CASE_RunPotocolDisplay;         
        }
        
        if (PointInRect(p, 228, 50, 100, 78)) // SUB MENU
        {

          drawSubMenuDisplay();
          caseUX = CASE_RunSubMenuDisplay;
          
        }
        
        if (PointInRect(p, 228, 138, 100, 100))  // SETTINGS
        {
          drawSettingsDisplay();
          caseUX = CASE_RunSettingsDisplay;
        }

      

      }
break; // case Main


case CASE_RunSubMenuDisplay:
runSubMenuDisplay();
break; //RunSettingsDisplay


case CASE_RunSettingsDisplay:
runSettingsDisplay();
break; //RunSettingsDisplay


case CASE_InitMeasurement:


x_pos = 0;
measurements=0;
leds.setAllBrightness(ledBrightness);
//SetLEDBrightness();
err=startCam();  // Start Cam
caseUX = CASE_RunMeasurement;
break; //CASE_InitMeasurement


case CASE_RunPotocolDisplay:
runProtocolDisplay();
break; //CASE_RunPotocolDisplay


case CASE_RunMeasurement:

      //myCamWriteRegister8(0,p.y);

      if (ts.touched()) {
        p = ts.getPoint();
        if (PointInRect(p, 320 - 70, 0, 70, 40)) {
          setHeaters(temperature_mean, 0,0);
          drawMainDisplay();
          caseUX = CASE_Main;
          leds.setAllBrightness(0);
          stopCam(); // Stop Cam
          break;  //CASE_RunMeasurement
        }
      }

    if (millis() - myTime_measure > MEASUREMENT_INTERVAL_MS) {

        MeasureCam();
        camSprite.pushSprite(0, 240 - 60);

        x_pos = x_pos + 1;
        if (x_pos > 280) {
          x_pos = 0;
          drawMeasurementDisplay();
        }

        for (int sensor = 0; sensor < NUM_SENSORS; sensor++) {
          if (x_pos > 1) tft.drawLine(20 + x_pos - 1, (240 - 71) - (last_values[sensor] / 2), 20 + x_pos, (240 - 71) - (fluorescence[sensor][0] / 2), my_palette[sensor]);
          last_values[sensor] = fluorescence[sensor][0];
        }

        myTime_measure = millis();
      }
      
break; //CASE_RunMeasurement


case CASE_RunComplete:

if (ts.touched()) {
        caseUX = CASE_Main;
        drawMainDisplay();
        stopCam();   // Ensure camera is stopped on return to main so the next USB save is not blocked.
        }
break; //CASE_RunComplete

case CASE_InitBaseline:
leds.setAllBrightness(ledBrightness);

err=startCam();  // Start Cam
caseUX = CASE_RunBaseline;

break; //CASE_InitBaseline


case CASE_InitCalibration:
leds.setAllBrightness(ledBrightness);

for (int sensor = 0; sensor < NUM_SENSORS; sensor++) {
       sensor_center_x[sensor] = sensor * SENS_SPACING + SENS_OFFSET_X;
       sensor_center_y[sensor] = SENS_OFFSET_Y;
}
          
err=startCam();  // Start Cam
caseUX = CASE_RunCalibration;

break; //CASE_InitCalibration


  
case CASE_RunInitUSB:

if(ts.touched()) {touch=true;p=ts.getPoint();if(p.y>0) tpoint = p;}

  if (!ts.touched()&&touch) {
  touch=false;

  
      if (PointInRect(tpoint, 20, 160, 130, 80)) {  //CANCEL
        drawMainDisplay();
        caseUX = CASE_Main;
        break;
      }

      if (PointInRect(tpoint, 170, 160, 130, 80)) { //FORMAT
         tft.println();
         tft.println("       FORMATTING...");

            esp_task_wdt_delete(NULL); // Stop Watchdog for this
        InitializeUSB();
            esp_task_wdt_add(NULL);// Start Watchdog again
        
        newConfigAvailable=true;
        // Reset Preferences
          for (int sensor = 0; sensor < NUM_SENSORS; sensor++) {
            wellFactor[sensor] = 1;
           preferences.putFloat("SenFactor"+ sensor, 1.0);
          }
  
        caseUX = CASE_Main;
        drawMainDisplay();

        break;
      }
  }
      
break; // CASE_RunInitUSB


case CASE_RunBaseline:

if(ts.touched()) {touch=true;p=ts.getPoint();if(p.y>0) tpoint = p;}

  if (!ts.touched()&&touch) {
  touch=false;
  
      if (PointInRect(tpoint, 20, 160, 130, 80)) { //CANCEL
        drawMainDisplay();
        stopCam(); // Stop Cam
        leds.setAllBrightness(0);
        caseUX = CASE_Main;

        break;
      }

      if (PointInRect(tpoint, 170, 160, 130, 80)) { //SAVE BASELINE
         SeeCam(true);
        stopCam(); // Stop Cam
        leds.setAllBrightness(0);

        saveBinToSPIFFS(baseBuf,SENS_WIDTH*SENS_HEIGHT ,"/base.bin");
        
        drawMainDisplay();
        caseUX = CASE_Main;

          
        break;
      }
  }
      SeeCam(false);
      camSprite.pushSprite(0, 80);
      
break; // CASE_RunBaseline

case CASE_RunCalibration:

if(ts.touched()) {touch=true;p=ts.getPoint();if(p.y>0) tpoint = p;}

  if (!ts.touched()&&touch) {
  touch=false;

      if (PointInRect(tpoint,20, 160, 130, 80)) { //CANCEL
        drawMainDisplay();
        stopCam(); // Stop Cam
        leds.setAllBrightness(0);

        for (int sensor = 0; sensor < NUM_SENSORS; sensor++) {
        sensor_center_x[sensor] = preferences.getInt("SenCentX" + sensor, sensor * SENS_SPACING + SENS_OFFSET_X);
        sensor_center_y[sensor] = preferences.getInt("SenCentY" + sensor, SENS_OFFSET_Y);
  }

  
        caseUX = CASE_Main;
        break;
      }

      if (PointInRect(tpoint, 170, 160, 130, 80)) { //SAVE ALIGNEMENT

         AlignCam(true);

  for (int sensor = 0; sensor < NUM_SENSORS; sensor++) {
   wellFactor[sensor] = 1;
  }
         measurements=0;
       if (saveWellFactors)  MeasureCam();

        stopCam(); // Stop Cam
        leds.setAllBrightness(0);
        drawMainDisplay();
        
  float maxFluorescence=0;
  
  for (int sensor = 0; sensor < NUM_SENSORS; sensor++) {
   if (fluorescence[sensor][0]>maxFluorescence) maxFluorescence=fluorescence[sensor][0];
  }


     for (int sensor = 0; sensor < NUM_SENSORS; sensor++) {
      if ((fluorescence[sensor][measurements]!=0)&&(saveWellFactors))
      wellFactor[sensor] = maxFluorescence/fluorescence[sensor][measurements];
   
      preferences.putInt("SenCentX" + sensor, sensor_center_x[sensor] );
      preferences.putInt("SenCentY" + sensor, sensor_center_y[sensor] );
      preferences.putFloat("SenFactor"+ sensor, wellFactor[sensor]);

       }

        saveMaskToSPIFFS((uint8_t *)maskBuf);

        caseUX = CASE_Main;
        break;
      } //SAVE ALIGNEMENT

     if (PointInRect(tpoint, 30, 65, 160, 155)) { //Toggle Well Factor
     saveWellFactors=!saveWellFactors;
  }
}
      AlignCam(false);
      camSprite.pushSprite(0, 65);
      tft.setCursor (65,156);
      tft.setFreeFont(&neuropol10pt7b); 
      if (saveWellFactors) tft.print("\x82"); else {tft.print("\x81");tft.fillRect(68,144,12,12,TFT_WHITE);}

break; // CASE_RunCalibration

case CASE_InitRun:

    p = ts.getPoint();
      if (PointInRect(p, 20, 160, 130, 80)) {
        drawMainDisplay();
        caseUX = CASE_Main;
        break;
      }

      if (PointInRect(p, 170, 160, 130, 80)) {
         
        caseUX = CASE_InitQPCR; 
        break;
      }
      
 
break; // CASE_InitRun

case CASE_InitQPCR:

measurements=0;
casePCR = PCR_HEATLID;
PCRstep = 0;
PCRcycle = 1;

TEMPLIDset=LidTemp;
TEMPset = pcrProtocol.steps[PCRstep].temperature;
TIMEcontrol = pcrProtocol.steps[PCRstep].duration*1000;

 tft.fillRect(0,0,320,240,TFT_WHITE);
 drawGrid();

  leds.setAllBrightness(ledBrightness);
  err=startCam();  // Start Cam
  delay(ILLUMINATION_TIME);
  MeasureCam();
  camSprite.pushSprite(0, 240 - 60);
  stopCam();
  leds.setAllBrightness(0);

openNewProtocolFile();
myTime_measure = millis();
      
caseUX = CASE_RunQPCR;
           
break; //CASE_InitQPCR



case CASE_AbortRunQPCR:

      if (ts.touched()) {
        p = ts.getPoint();
        if (PointInRect(p, 15,100,80,50)) {
          setHeaters(temperature_mean, 0,0);
          stopCam(); // Stop Cam
          addDataToUSB();
          drawMainDisplay();
          caseUX = CASE_Main;
          break;
        }

         if (PointInRect(p, 117, 100, 110, 50)) {
          caseUX = CASE_RunQPCR;

         }
      }
   
case CASE_RunQPCR:

      // Reset robustness counters at the start of each run.
      cameraFailCount = 0;
      writeFailCount = 0;

      if (ts.touched()) {
        p = ts.getPoint();
        if (PointInRect(p, 320 - 60, 0, 60, 50)) {
          if (casePCR == PCR_END) 
          { drawMainDisplay();
          caseUX = CASE_Main;
          break;} else
           caseUX = CASE_AbortRunQPCR;
        }
      }

    readTemperatures();
    
      temperature_mean = (temperature_mean * 3 + temperature) / 4;
      temperature_lid_mean = (temperature_lid_mean * 3 + temperature_lid) / 4;



      PIDIntegration = false;

      TEMPdif = TEMPset - temperature_mean;
      if(abs(TEMPdif<3)) {TEMPi = TEMPi + (TEMPset - temperature_mean);} else TEMPi=0;
      if (TEMPi > TEMPiMax) TEMPi = TEMPiMax;
      if (TEMPi < -TEMPiMax) TEMPi = -TEMPiMax;

      TEMPLIDdif = TEMPLIDset - temperature_lid_mean;
      if(abs(TEMPLIDdif<PIDLIDiRange)) {TEMPLIDi = TEMPLIDi + (TEMPLIDset - temperature_lid_mean);} else TEMPLIDi=0;
      if (TEMPLIDi > TEMPLIDiMax) TEMPLIDi = TEMPLIDiMax;
      if (TEMPLIDi < -TEMPLIDiMax) TEMPLIDi = -TEMPLIDiMax;
      //Serial.print("Temp: ");
      //Serial.println(temperature_mean);
      //Serial.print("Dif: ");
      //Serial.println(TEMPdif);



// Draw Grid (42ms)
 drawGrid();


      if (millis() - TEMPclick > 200) {
        TEMPclick = millis();
        TEMPd = TEMPdif_a[4] - TEMPdif;
        TEMPdif_a[4] = TEMPdif_a[3];
        TEMPdif_a[3] = TEMPdif_a[2];
        TEMPdif_a[2] = TEMPdif_a[1];
        TEMPdif_a[1] = TEMPdif_a[0];
        TEMPdif_a[0] = TEMPdif;
        //  Serial.println (TEMPd);


       // tft.drawLine(x_pos, 230 - ((int)(temperature_mean * 10)) % 180, x_pos, 230 - ((int)(temperature_mean * 10)) % 180, my_palette[4]);

       // x_pos++;
      //  if (x_pos > 300)x_pos = 0;
      }



      switch (casePCR) {


        case PCR_HEATLID:

        runPID();

        if (TEMPLIDdif<1) casePCR = PCR_SET;

         break;

                  
        case PCR_SET:
          TEMPset = pcrProtocol.steps[PCRstep].temperature;
          TIMEcontrol = pcrProtocol.steps[PCRstep].duration*1000;
          PIDIntegration = false;
          casePCR = PCR_TRANSITION;
          break;




        
        case PCR_TRANSITION:
          runPID();
          //draw_run_display()

          // [追加] 停止要求があれば即座に終了
          if (stopRequested) {
            setHeaters(temperature_mean, 0,0);
            stopCam();
            addDataToUSB();
            casePCR = PCR_END;
            stopRequested = false;
            break;
          }

          if (abs(TEMPset - temperature_mean) < TEMP_TOLLERANCE) {
            PIDIntegration = true;
            TIMEclick = millis();
            casePCR = PCR_TIME;
          }
          break;


        case PCR_TIME:
          runPID();
          TIMEcontrol = pcrProtocol.steps[PCRstep].duration*1000 - (millis() - TIMEclick) ;
          // draw_run_display();
          //Serial.println(PCRstep);
          // Serial.println(pcrProtocol.repeatEnd);

          if (pcrProtocol.steps[PCRstep].capture&&(TIMEcontrol <= ILLUMINATION_TIME)&&!saveReady) {initSaveMeasurement(); saveReady=true;};

          // [追加] 停止要求があれば即座に終了
          if (stopRequested && !saveReady) {
            setHeaters(temperature_mean, 0,0);
            stopCam();
            addDataToUSB();
            casePCR = PCR_END;
            stopRequested = false;
            break;
          }

          if (TIMEcontrol <= 0) {
              
            if (pcrProtocol.steps[PCRstep].capture)   {measurements++; measureTemp = temperature_mean; SaveMeasurement(8);saveReady=false;}
            // Flash the screen red briefly if a camera capture or data write failed,
            // so the operator notices without having to read the serial log.
            if (cameraFailCount > 0 || writeFailCount > 0) {
              tft.fillScreen(TFT_RED);
              vTaskDelay(150 / portTICK_PERIOD_MS);
              drawGrid();
            }
            
            if (PCRstep == pcrProtocol.repeatEnd-1)
            {PCRcycle++;
             PCRstep = pcrProtocol.repeatStart-1;
             if (PCRcycle>pcrProtocol.cycleCount) 
             {PCRstep=pcrProtocol.repeatEnd;PCRcycle=1;}
            }
             else 
             {PCRstep++;}
            
            casePCR = PCR_SET;
            
            if (PCRstep >= pcrProtocol.stepCount)  
            { setHeaters(temperature_mean, 0,0);
              stopCam(); // Stop Camcase
              addDataToUSB();
              casePCR = PCR_END;
              }
         
            
          }
          break;

        case PCR_END:

         break;

         
        default:
          // Statement(s)
          break;


      }//PCR switch
      break; // CASE_RunQPCR




 
case CASE_RunWIFI:
runWifiDisplay();
break; // CASE_RunWIFI


case CASE_EditSettings:

     

break; // case CASE_EditSettings

  

}  // SWITCH


}


void initSaveMeasurement()
{
      Serial.println("initSaveMeasurement");

  leds.setAllBrightness(ledBrightness);
  err=startCam();  // Start Cam
  
}

void SaveMeasurement(int step_size)
{
     Serial.println("SaveMeasurement");
  setHeaters(0,0, 0);

  //  analogWrite(EX_LEDS, 20);  // turn the LED on (HIGH is the voltage level)  <<<<<<<<<<<<<<<<<<<<
  //vTaskDelay(1500 / portTICK_PERIOD_MS); // The LED needs to be turned on ~150ms before the call to esp_camera_fb_get()
  MeasureCam();
  stopCam();
  camSprite.pushSprite(0, 240 - 60);

  // analogWrite(EX_LEDS, 0);  // turn the LED on (HIGH is the voltage level) <<<<<<<<<<<<<<<<<<<<
  leds.setAllBrightness(0);

  // pinMode(EX_LEDS, OUTPUT); // Excitation LEDs
  // digitalWrite(EX_LEDS,LOW);   // turn the LED on (HIGH is the voltage level)

  Serial.print(myFileName);
  File file = SPIFFS.open(myFileName, FILE_APPEND);
  if (!file) {
    // Cannot open the data file for appending. Report it and skip this point
    // instead of silently writing to a closed handle (which would corrupt or
    // lose the measurement record).
    writeFailCount++;
    Serial.println("- failed to open file for appending");
    return;
  }

  file.print(PCRcycle);
  file.print(", ");
  file.print(int((millis() - myTime_measure)/1000),0);
  file.print(", ");
  file.print(measureTemp, 1);

  for (int sensor = 0; sensor < NUM_SENSORS; sensor++) {
    file.print(", ");
    file.print(fluorescence[sensor][measurements]);
  }
  file.println();
  file.close();
  Serial.println(" ok");
} // SaveMeasurment



void MeasureCam()
{
  const int sensDispSizeX = 50;
  const int sensDispSizeY = 50;
  const int sens_size_x = 20;
  const int sens_size_y = 20;

  const float disp_SENS_SPACING = 36;

  long intensity_sum;
  int intensityCount;
  int intensity;
 

  camera_fb_t * image_fb = NULL;

  // Retry camera capture a few times. A single transient read failure must not
  // drop the whole measurement point (important for HRM curve continuity).
  const int CAM_RETRIES = 3;
  int camRetries = 0;
  while ((image_fb == NULL) && (camRetries < CAM_RETRIES)) {
    image_fb = esp_camera_fb_get();
    if (!image_fb) {
      camRetries++;
      vTaskDelay(2 / portTICK_PERIOD_MS); // brief settle before retry
    }
  }

  if (!image_fb)
  {
    ESP_LOGE("my", "Camera capture failed after retries");
    cameraFailCount++;
    // Keep the previous fluorescence value so the curve stays continuous.
    for (int sensor = 0; sensor < NUM_SENSORS; sensor++) {
      fluorescence[sensor][measurements] = last_values[sensor];
    }
  } else
  {

    camSprite.fillRect(0, 0, camSprite.width(), camSprite.height(), TFT_BLACK);
    
    long i = 0;
    long ibuf = 0;
    

 //  // Measure Sensor Intensities + Draw Sensor Images
    for (int sensor = 0; sensor < NUM_SENSORS; sensor++) {
      intensity_sum = 0;
      intensityCount=0;
      for (int y = 0; y < sensDispSizeY; y++) {
        for (int x = 0; x < sensDispSizeX; x++) {
          
          i = sensor_center_x[sensor] - sensDispSizeX / 2 + x + (y + sensor_center_y[sensor] - sensDispSizeY / 2) * (image_fb->width);
          ibuf = i -(SENS_OFFSET_Y - camSprite.height()) *image_fb->width;
          
         if (baseBuf[ibuf]<255) intensity = static_cast<int>((image_fb->buf[i] - baseBuf[ibuf]) * (255/(255.0 - baseBuf[ibuf]))); else intensity=255;

                     if (maskBuf[ibuf]) {intensity_sum = intensity_sum + intensity; intensityCount++;}


          if (intensity<0) intensity=0;
            camSpriteBuf[(int)(sensor * disp_SENS_SPACING) + x / 2 + 21 + (y / 2 + 20)*camSprite.width()] = ((intensity & 0x1c) << 11) | ((intensity & 0xe0) >> 5) | ((intensity & 0x07) << 8);
         // camSpriteBuf[(int)(sensor * disp_SENS_SPACING) + x / 2 + 21 + (y / 2 + 20)*camSprite.width()] = maskBuf[ibuf]*0xff;

        } //for x
      } //for y
            fluorescence[sensor][measurements] = (float)intensity_sum*wellFactor[sensor]/ intensityCount;
      last_values[sensor] = fluorescence[sensor][measurements];

    }//for Sensors




    /*
      for (int y = 0; y < camSprite.height(); y++) {
        for (int x = 0; x < (camSprite.width()); x++) {
          //image_fb->width 320,120
          uint8_t intensity = (image_fb->buf[x * 2 + (y * 2 + SENS_OFFSET_Y - 40) * image_fb->width]);
          camSpriteBuf[x + y * camSprite.width()] = ((intensity & 0x1c) << 11) | ((intensity & 0xe0) >> 5) | ((intensity & 0x07) << 8);

        }
      }
    */
    camSprite.setFreeFont(&GaudiSans7pt7b); 
    camSprite.setTextColor(TFT_GREEN, TFT_BLACK);

    for (int sensor = 0; sensor < 8; sensor++) {

      camSprite.setTextDatum(MC_DATUM);
      camSprite.setTextColor(my_palette[sensor], TFT_BLACK);
      camSprite.drawString(String(fluorescence[sensor][measurements],0), 160 +17+ (sensor - 4)*disp_SENS_SPACING,51);
      camSprite.drawString(String(sensor+1),160 +17+ (sensor - 4)*disp_SENS_SPACING,11);
      camSprite.fillRect(160 + (sensor - 4)*disp_SENS_SPACING,0,disp_SENS_SPACING,4,  my_palette[sensor]);

      /*
        Serial.print("intensity ");
        Serial.print(sensor);
        Serial.print(" : ");
        Serial.println(fluorescence[sensor]);
      */

    } // for draw



    // camSprite.pushImage(-200, -200, 640, 480, (uint16_t *)image_fb->buf);

    esp_camera_fb_return(image_fb);

  }
} // MeasureCam

void SetLEDBrightness(boolean state)   // Not currently used
{
  const uint8_t LEDBrightness[]
  {225,225,225,225,225,225,225,225};
  
   if (state) for (byte i = 0; i < NUM_SENSORS; i++)
  {
    //  leds.setBrightness(i-1,0);
    leds.setBrightness(i, LEDBrightness[i]);
  } else 
    leds.setAllBrightness(0);

  
  }

void SeeCam(boolean saveBase)
{
  camera_fb_t * image_fb = NULL;
  image_fb = esp_camera_fb_get();

  if (!image_fb)
  {
    ESP_LOGE("my", "Camera capture failed");
  } else {

    for (int y = 0; y < camSprite.height(); y++) {
      for (int x = 0; x < (camSprite.width()); x++) {
        //image_fb->width 320,120
        uint8_t intensity = (image_fb->buf[x * 2 + (y * 2 + SENS_OFFSET_Y - camSprite.height()) * image_fb->width]);
        camSpriteBuf[x + y * camSprite.width()] = ((intensity & 0x1c) << 11) | ((intensity & 0xe0) >> 5) | ((intensity & 0x07) << 8);

      }
    }

 if (saveBase){
       for (int y = 0; y < SENS_HEIGHT; y++) {
      for (int x = 0; x < (SENS_WIDTH); x++) {
        
        uint8_t intensity = (image_fb->buf[x  + (y  + SENS_OFFSET_Y - camSprite.height()) *image_fb->width]);
        baseBuf[x + y * SENS_WIDTH] = intensity ;

      }
    }
 } // saveBase
 


    // camSprite.pushImage(-200, -200, 640, 480, (uint16_t *)image_fb->buf);
    esp_camera_fb_return(image_fb);
  }
} // See Cam



void AlignCam(boolean saveMask)
{
  const int sens_size_x = 60;
  const int sens_size_y = 60;

  const float disp_SENS_SPACING = 36;

  long intensity;
  long center_x;
  long center_y;

  camera_fb_t * image_fb = NULL;
  image_fb = esp_camera_fb_get();

  if (!image_fb)
  {
    ESP_LOGE("my", "Camera capture failed");
  } else {

    long i = 0;
    long ibuf = 0;


//Find Center
    for (int sensor = 0; sensor < NUM_SENSORS; sensor++) {
      intensity = 0;
      center_x = 0;
      center_y = 0;
        
      for (int y = 0; y < sens_size_y; y++) {
        for (int x = 0; x < sens_size_x; x++) {

        i = sensor_center_x[sensor] - sens_size_x / 2 + x + (y + sensor_center_y[sensor] - sens_size_y / 2) * (image_fb->width);
        ibuf= i -(SENS_OFFSET_Y - camSprite.height()) *image_fb->width;


        if ((i<307200)&&(i>0)&&(ibuf>0)&&(ibuf<(SENS_WIDTH*SENS_HEIGHT)))
        {
          intensity = intensity + image_fb->buf[i]-baseBuf[ibuf];
          center_x = center_x + (image_fb->buf[i]-baseBuf[ibuf]) * x;
          center_y = center_y + (image_fb->buf[i]-baseBuf[ibuf]) * y;
         // if (image_fb->buf[i]>baseBuf[ibuf])
         // image_fb->buf[i] = (image_fb->buf[i]-baseBuf[ibuf]) | 0x1f; else image_fb->buf[i] =0;
         }
         
        } //for x
      } //for y
 
      sensor_center_x[sensor] = sensor_center_x[sensor] + center_x / intensity - sens_size_x / 2;
      sensor_center_y[sensor] = sensor_center_y[sensor] + center_y / intensity - sens_size_y / 2;

       if (sensor_center_y[sensor] < SENS_OFFSET_Y-SENS_SPACING/3) sensor_center_y[sensor]= SENS_OFFSET_Y-SENS_SPACING/3;
       if (sensor_center_y[sensor] > SENS_OFFSET_Y+SENS_SPACING/3) sensor_center_y[sensor]= SENS_OFFSET_Y+SENS_SPACING/3;
      
       if (sensor_center_x[sensor] < sensor * SENS_SPACING-SENS_SPACING/3 + SENS_OFFSET_X) sensor_center_x[sensor]=sensor * SENS_SPACING-SENS_SPACING/3 + SENS_OFFSET_X;
       if (sensor_center_x[sensor] > sensor * SENS_SPACING+SENS_SPACING/3 + SENS_OFFSET_X) sensor_center_x[sensor]=sensor * SENS_SPACING+SENS_SPACING/3 + SENS_OFFSET_X;

       
     // fluorescence[sensor] = (float)intensity / sens_size_x / sens_size_y;
    
    }//for Sensors

//Extract Sensor Mask

       for (int y = 0; y < SENS_HEIGHT; y++) {
      for (int x = 0; x < (SENS_WIDTH); x++) {
        
        int intensity = (image_fb->buf[x  + (y  + SENS_OFFSET_Y - camSprite.height()) *image_fb->width]-baseBuf[x + y * SENS_WIDTH]);
        if (intensity<0) intensity=0;
       if (saveMask) maskBuf[x + y * SENS_WIDTH] = intensity>MASK_THRESHOLD ;
      // camSpriteBuf[x/2 + y/2 * camSprite.width()] = ((intensity & 0x1c) << 11) | ((intensity & 0xe0) >> 5) | ((intensity & 0x07) << 8);
       camSpriteBuf[x/2 + y/2 * camSprite.width()] = 0x1ce007 *(intensity>MASK_THRESHOLD);

      }
    }

 // Draw Center Cross  
    for (int sensor = 0; sensor < NUM_SENSORS; sensor++) {
      camSprite.drawLine(sensor_center_x[sensor] / 2, (sensor_center_y[sensor] - SENS_OFFSET_Y + camSprite.height()) / 2 - 5, sensor_center_x[sensor] / 2, (sensor_center_y[sensor] - SENS_OFFSET_Y + camSprite.height()) / 2 + 5, TFT_RED);
      camSprite.drawLine((sensor_center_x[sensor]) / 2 - 5, (sensor_center_y[sensor] - SENS_OFFSET_Y + camSprite.height()) / 2, (sensor_center_x[sensor]) / 2 + 5, (sensor_center_y[sensor] - SENS_OFFSET_Y + camSprite.height()) / 2, TFT_RED);
    }

    esp_camera_fb_return(image_fb);

    // camSprite.pushImage(-200, -200, 640, 480, (uint16_t *)image_fb->buf);

  }


} // Calibrate Cam





void myCamWriteRegister8(uint8_t reg, uint8_t val) {
  // use i2c
  Wire.beginTransmission(0x21);
  Wire.write((byte)reg);
  Wire.write((byte)val);
  Wire.endTransmission();
}

//// PID

void readTemperatures()
{
  float ADCVoltage = 0;


    tla.setChannel((tla202x_channel_t)ADC_VADC);
      delay(10);
      ADCVoltage = tla.readVoltage();
      
      tla.setChannel((tla202x_channel_t)ADC_BlockTemp);
      delay(10);
      sensorVoltage = tla.readVoltage();

      sensorResistance = ((sensorVoltage * NTC_R0) / (ADCVoltage - sensorVoltage));
      logR = log(sensorResistance * 1000);
      temperature = 1 / (NTC_A + NTC_B * logR + NTC_C * pow(logR, 3))- 273.15;
     // temperature =  1 / (log(sensorResistance * 1000 / NTC_RN) / NTC_B + 1 / NTC_TN) - 273.15 ;
     

      tla.setChannel((tla202x_channel_t)ADC_LidTemp);
      delay(10);
      sensorVoltage = tla.readVoltage();

      sensorResistance = ((sensorVoltage * NTC_R0) / (ADCVoltage - sensorVoltage));
      temperature_lid =  1 / (log(sensorResistance * 1000 / NTC_RN) / NTC_LID_B + 1 / NTC_TN) - 273.15 ;

if (!isTemperatureSafe(temperature)) {errorCode=ERROR_TEMP_SENSOR;emergencyShutdown();}
if (!isTemperatureSafe(temperature_lid)) {errorCode=ERROR_TEMP_SENSOR;emergencyShutdown();}

myTime_WD=millis();
esp_task_wdt_reset(); // Feed the watchdog  
  }

int power_heating(float temperature, float power)
{
  float power_return;
#define FA_c -0.000016878
#define FA_d -0.00285
#define FA_e 0.004473
#define FA_f 0.06775

  power_return =(power-FA_d*temperature-FA_f)/(FA_c*temperature+FA_e);

  if (power_return > 255) power_return = 255;
  if (power_return < 0) power_return = 0;

  return (int)power_return;

}

int power_cooling(float temperature, float power)
{
  float power_return;

#define FA_g -0.2275
#define FA_h 0.589
#define FA_cTl 25
#define FA_cTh 60


  power_return = pow(2.7, ((power * (FA_cTh - FA_cTl) / (temperature - FA_cTl)) - FA_h) / FA_g);

  if (power_return > 255) power_return = 255;
  if (power_return < 0) power_return = 0;

  return (int)power_return;
}

int power_lid_heating(float power)
{
  float power_return;

#define FA_g 0.0
#define FA_h 0.0156

power_return = (power + FA_g)/FA_h;

  if (power_return > 255) power_return = 255;
  if (power_return < 0) power_return = 0;

  return (int)power_return;

}


void runPID()
{

    TEMPcontrol = PIDp * TEMPdif + PIDd * TEMPd +  PIDi * TEMPi;
    TEMPLIDcontrol = PIDLIDp * TEMPLIDdif+  PIDLIDi * TEMPLIDi;
    setHeaters(temperature_mean, TEMPcontrol,TEMPLIDcontrol); 


} //runPID

void setHeaters(float temperature, float blockPower,float lidPower)
{
  int blockPWM=0;
  int fanPWM=0;
  int lidPWM=0;




lidPWM = power_lid_heating(lidPower);

      //  tft.drawLine(x_pos, 170 - lidPWM/2, x_pos, 170 - lidPWM/2, my_palette[1]);


    if (casePCR != PCR_HEATLID)
    if (blockPower > 0)
    {
      blockPWM = power_heating(temperature, blockPower);
      fanPWM=0;

      
      if (LidCurrentMax/255*lidPWM + BlockCurrentMax/255*blockPWM > CurrentMax) blockPWM= (CurrentMax-LidCurrentMax/255*lidPWM)/BlockCurrentMax*255;
      
      if (blockPWM > 255) blockPWM = 255;
      if (blockPWM < 0) blockPWM = 0;

  
    }
    else
    {
      fanPWM = power_cooling(temperature, blockPower);
      blockPWM=0;

      if (LidCurrentMax/255*lidPWM + BlockCurrentMax/255*fanPWM > CurrentMax) fanPWM= (CurrentMax-LidCurrentMax/255*lidPWM)/FanCurrentMax*255;
      
      if (fanPWM > 255) fanPWM = 255;
      if (fanPWM < 0) fanPWM = 0;
    }


//Serial.print("fanPWM: ");
//Serial.println(fanPWM);

//Serial.print("blockPWM: ");
//Serial.println(blockPWM);

//Serial.print("lidPWM: ");
//Serial.println(lidPWM);
//Serial.println("");


  ledcWrite(FAN_CHANNEL, fanPWM);  // Set the PWM duty cycle for FAN 
  ledcWrite(HEATER_CHANNEL, blockPWM);  // Set the PWM duty cycle for HEATER 
  ledcWrite(LID_CHANNEL, lidPWM);  // Set the PWM duty cycle for HEATER 

}

esp_err_t startCam()
{
  
  // Init camera
  camera_config_t config;

  config.ledc_channel = LEDC_CHANNEL_0; //?
  config.ledc_timer = LEDC_TIMER_0;   //?
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000; //20000000

  config.pin_sccb_sda = -1;
  config.sccb_i2c_port =0;


  // config.pixel_format = PIXFORMAT_JPEG;
  // config.pixel_format = PIXFORMAT_RGB565;
  config.pixel_format = PIXFORMAT_GRAYSCALE;
  //  config.pixel_format = PIXFORMAT_YUV422;


  //config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.grab_mode = CAMERA_GRAB_LATEST;

  config.jpeg_quality = 12;
  config.fb_count = 2;

  config.frame_size = FRAMESIZE_VGA;

  // config.fb_location = CAMERA_FB_IN_DRAM;
  config.fb_location = CAMERA_FB_IN_PSRAM;


  //FRAMESIZE_240X240     FRAMESIZE_SVGA  (800 x 600)      FRAMESIZE_VGA(640 x 480)   FRAMESIZE_QVGA  (320 x 240)

  esp_err_t err = esp_camera_init(&config);

  cam_sens = esp_camera_sensor_get();
 
  res = cam_sens->set_vflip(cam_sens, 1);
  res = cam_sens->set_hmirror(cam_sens, 1);

  res = cam_sens->set_brightness(cam_sens, 1); // up the brightness just a bit
  res = cam_sens->set_whitebal(cam_sens, 0);     // set automatic white balance
  res = cam_sens->set_exposure_ctrl(cam_sens, 0); // set automatic exposure
  res = cam_sens->set_awb_gain(cam_sens, 0);  // set automatic white balance gain
  res = cam_sens->set_aec2(cam_sens, 0);  // aec DSP
  res = cam_sens->set_gain_ctrl(cam_sens, 0); // set automatic gain control

  res = cam_sens->set_reg(cam_sens, 0x00, 0xFF, 16);     //set gain (0-127?) Gain=(GAIN[7]+1)×(GAIN[6]+1)×(GAIN[5]+1)×(GAIN[4]+1)×(GAIN[3:0]/16+1)
  res = cam_sens->set_aec_value(cam_sens, 512);        //set exposure (0-512?)

  res = cam_sens->set_reg(cam_sens, 0x46, 0x07, 1);  // set Lens correction    
  //res = cam_sens->set_reg(cam_sens, 0x47, 0xFF, 15);  // set Lens center 
  res = cam_sens->set_reg(cam_sens, 0x49, 0xFF, 150);    // set Lens correction factor  200
  res = cam_sens->set_reg(cam_sens, 0x4A, 0xFF, 10);    // set Lens correction mid Radius 15

  // res = cam_sens->set_reg(cam_sens, 0x12, 0x3, 3);     //set RAW?
  //cam_sens->set_pixformat(cam_sens, pf);
  // res = cam_sens->set_pixformat(cam_sens, PIXFORMAT_RGB565);

  cameraOn=true;
return err;
}

esp_err_t stopCam()
{
    esp_err_t err = esp_camera_deinit();
    cameraOn=false;
   return err;
  }



  
void openNewProtocolFile()
{

  File file = SPIFFS.open(myFileName, FILE_WRITE);
  if (!file) {
    Serial.println("- failed to open file for appending");
      bool formatted = SPIFFS.format();
      if(formatted){
      Serial.println("\n\nSuccess formatting");
      }else{
      Serial.println("\n\nError formatting");
      }
    
  }
file.print("Protocol name: ");
file.println(pcrProtocol.name);
file.println("Cycle, Time, Temp, Sensor1, Sensor2, Sensor3, Sensor4, Sensor5, Sensor6, Sensor7, Sensor8");

}


void addDataToUSB()
{
  InitializeUSBFiles();
  addFileToFAT(SPIFFS,myFileName);

  Serial.println("InitializeUSBFiles");
}

void initMask()
{
  
   for (int i = 0; i < SENS_WIDTH*SENS_HEIGHT; i++) maskBuf[i]=false;  


  const int sens_size_x = 20;
  const int sens_size_y = 20;

 // Measure Sensor Intensities
    long i = 0;

    for (int sensor = 0; sensor < NUM_SENSORS; sensor++) {
      for (int y = 0; y < sens_size_y; y++) {
        for (int x = 0; x < sens_size_x; x++) {

          //  i=sensor*SENS_SPACING+x+SENS_OFFSET_X+(y+SENS_OFFSET_Y)*(image_fb->width);
          i = sensor_center_x[sensor] - sens_size_x / 2 + x + (y-SENS_OFFSET_Y+camSprite.height() + sensor_center_y[sensor] - sens_size_y / 2) * (SENS_WIDTH);
           maskBuf[i]=true;
        } //for x
      } //for y
    }//for Sensors


}

int countCaptures()
{int captureCounter=0;

for (int stepCounter = 0; stepCounter < pcrProtocol.stepCount; stepCounter++) {

if (pcrProtocol.steps[stepCounter].capture)
{if ((stepCounter<=pcrProtocol.repeatEnd-1)&&(stepCounter>=pcrProtocol.repeatStart-1))
captureCounter=captureCounter+pcrProtocol.cycleCount; else
captureCounter=captureCounter+1;
  
  }

}
  return captureCounter;
}




void  drawGrid()

{

  #define TFT_BUTTONCOLOR    0x3575


const int minSpacing = 30;       // Minimum spacing in pixels
const int grid_w = 230;
const int grid_h = 128;
const int margin_left = 8;
const int margin_top = 30;

// Clear Buffer
 tftbuff.fillRect(0, 0, 320, 180, TFT_WHITE);

// Draw Back Button
 tftbuff.setTextColor(TFT_WHITE,TFT_BLACK);
 tftbuff.setFreeFont(&neuropol10pt7b); 
 tftbuff.fillRoundRect(320-52,3,45,33,12,TFT_BUTTONCOLOR);
 tftbuff.setCursor (320-40,25);
 tftbuff.print(char(0x80));
 tftbuff.setTextColor(TFT_BLACK,TFT_WHITE);


  int division=1;
  // Guard against a protocol with no capture steps: captures==0 would make the
  // divisor below zero and crash the ESP32 (integer divide by zero).
  int captureCount = captures > 0 ? captures : 1;
  int xSpacing=grid_w/captureCount;
  if (xSpacing<minSpacing) {division=ceil(minSpacing*captureCount/grid_w);xSpacing=division*grid_w/captureCount;}

  int yMax=INT_MIN;
  int yMin=INT_MAX;

for (int x = 0; x <= measurements; x++)
for (int sensor = 0; sensor < NUM_SENSORS; sensor++) {
int value = fluorescence[sensor][x];
      
      if (value > yMax) {yMax = value;}
      if (value < yMin) {yMin = value;}
}

  int yStep=10*ceil((yMax-yMin)/70.0);
  if (yMax-yMin<35) yStep=5;
  int yMinLine=yStep*floor((float)yMin/yStep);
  int ySpacing = grid_h/8;
  int yLines = grid_h / ySpacing;



  tftbuff.setTextDatum(MC_DATUM);
  tftbuff.setFreeFont(&GaudiSans7pt7b); 

  // Draw vertical lines and labels (captures)
  for (int i = 0; i <= (captureCount/division); i++) {
    int x = i * xSpacing;
    tftbuff.drawLine(x+margin_left, margin_top, x+margin_left, margin_top+grid_h, TFT_LIGHTGREY);
     String label= String(i*division);
    tftbuff.drawString(label, x + margin_left, grid_h+margin_top+8);
  }
    tftbuff.drawLine(grid_w+margin_left, margin_top, grid_w+margin_left, margin_top+grid_h, TFT_LIGHTGREY);


  // Draw horizontal lines and labels (ranges)
  for (int j = 0; j <= yLines; j++) {
    int y = j * ySpacing;
    tftbuff.drawLine(margin_left, y+margin_top, margin_left+grid_w, y+margin_top, TFT_LIGHTGREY);
  //  tftbuff.setCursor(0, y + 2);
  //  tftbuff.print("R");
 //   tftbuff.print(j);
  }
    tftbuff.drawLine(margin_left, grid_h+margin_top, margin_left+grid_w, grid_h+margin_top, TFT_LIGHTGREY);

if (((yMinLine+8*yStep)>=0)&&(yMinLine<=0))
    tftbuff.drawLine(margin_left, grid_h+margin_top+yMinLine/yStep*ySpacing, margin_left+grid_w, grid_h+margin_top+yMinLine/yStep*ySpacing, TFT_DARKGREY);


// Draw Measurements

for (int x = 0; x < measurements; x++)
for (int sensor = 0; sensor < NUM_SENSORS; sensor++) {

tftbuff.drawLine(margin_left+x*((float)xSpacing/division),grid_h+margin_top+(yMinLine-(fluorescence[sensor][x]))/yStep*ySpacing,margin_left+(x+1)*((float)xSpacing/division),grid_h+margin_top+(yMinLine-(fluorescence[sensor][x+1]))/yStep*ySpacing,my_palette[sensor]);


  }


  
 // Write Step
      
      tftbuff.setTextColor(TFT_BLACK, TFT_WHITE);
      tftbuff.setFreeFont(&GaudiSans7pt7b);

      tftbuff.setCursor (20, 18);
      if (casePCR == PCR_HEATLID){
        tftbuff.print("Heating Lid");}
      else 
      if (casePCR == PCR_END){
        tftbuff.print("== RUN COMPLETE ==");}
      else
      {
      tftbuff.print("Step ");
      tftbuff.print(PCRstep+1);
      tftbuff.print(": ");
      tftbuff.print(pcrProtocol.steps[PCRstep].name); 
      }


  // Write Status
      tftbuff.setCursor (255, 60);
      tftbuff.print(char(0x83)); // Temp
      tftbuff.print(temperature_mean, 1);
      tftbuff.print("°C"); 

      tftbuff.setCursor (255, 75);
      tftbuff.print(char(0x84)); // Set Temp
      tftbuff.print(TEMPset,1);
      tftbuff.print("°C"); 

      tftbuff.setCursor (255, 90);
      tftbuff.print(char(0x86));
      tftbuff.print(TIMEcontrol/1000,0);
      tftbuff.print("s"); 

      tftbuff.setCursor (255, 105);
      tftbuff.print(char(0x85));
      tftbuff.print(PCRcycle); 
      tftbuff.print(" / "); 
      tftbuff.print(pcrProtocol.cycleCount);

      tftbuff.setCursor (255, 120);
      tftbuff.print("L ");
      tftbuff.print(temperature_lid_mean,0); 
      tftbuff.print("°C"); 




      // Draw Cancle Frame

      if (caseUX == CASE_AbortRunQPCR)
      {
  tftbuff.fillRect(15, 40, 215, 110, TFT_WHITE);
  tftbuff.drawRect(15, 40, 215, 110, TFT_DARKGREY);
  tftbuff.setFreeFont(&neuropol10pt7b); 
  tftbuff.setCursor (20,65);
  tftbuff.println("Abort PCR run?");
  tftbuff.setCursor (35,85);
  tftbuff.setFreeFont(&GaudiSans7pt7b);
  tftbuff.println("Current data will be safed.");

  tftbuff.setTextColor(TFT_WHITE,TFT_DARKGREY);
  tftbuff.fillRoundRect(25,100,80,40,12,TFT_DARKGREY);
  tftbuff.drawString("ABORT",64,119);
  tftbuff.fillRoundRect(117,100,100,40,12,TFT_DARKGREY);
  tftbuff.drawString("RESUME",168,119);
      }
      
      tftbuff.pushSprite(0, 0);

      

}

bool isTemperatureSafe(float temp) {
    return (temp >= SAFETY_MIN_TEMP && 
            temp <= SAFETY_MAX_TEMP);
}


void emergencyShutdown() {

// Immediately shut off all heaters

  pinMode(HEATER_PWM_PIN, OUTPUT);
  digitalWrite(HEATER_PWM_PIN, LOW);

  pinMode(LID_PWM_PIN, OUTPUT);
  digitalWrite(LID_PWM_PIN, LOW);

 // ledcWrite(HEATER_CHANNEL, 0);
 // ledcWrite(LID_CHANNEL, 0);
 
// Full fan power for cooling
  pinMode(FAN_PWM_PIN, OUTPUT);
  digitalWrite(FAN_PWM_PIN, HIGH);

  //  ledcWrite(FAN_CHANNEL, 255); // Full fan power for cooling

    
    // Turn off LEDs
    leds.setAllBrightness(0);


 
    // Display error on screen
    tft.fillScreen(TFT_RED);
    tft.setTextColor(TFT_WHITE);
    tft.setFreeFont(&neuropol10pt7b);
    tft.setCursor(35, 50);
    tft.println("SYSTEM ERROR !");
    tft.setCursor(35, 80);
    tft.setFreeFont(&GaudiSans7pt7b);
    if (errorCode==ERROR_TEMP_SENSOR) tft.println("TEMPERATURE OUT OF RANGE.");
    if (errorCode==ERROR_WATCHDOG) tft.println("CODE ERROR.");


  while (true) //!ts.touched()
  {esp_task_wdt_reset();} // Feed the watchdog 
  
}

// ==================== Web Server Handlers ====================

int getProgress() {
  if (casePCR == PCR_END) return 100;
  if (pcrProtocol.stepCount == 0) return 0;
  
  if (pcrProtocol.melt && pcrProtocol.meltPoints > 0) {
    return (int)((float)measurements / pcrProtocol.meltPoints * 100);
  } else if (pcrProtocol.cycleCount > 0) {
    return (int)((float)PCRcycle / pcrProtocol.cycleCount * 100);
  }
  return 0;
}

String getModeString() {
  if (casePCR == PCR_END) return "COMPLETE";
  if (casePCR == PCR_HEATLID && caseUX != CASE_RunQPCR) return "IDLE";
  if (caseUX != CASE_RunQPCR) return "IDLE";
  if (pcrProtocol.melt) return "HRM";
  return "PCR";
}

// Serve the standalone HRM-capable protocol builder page.
// The full HTML is embedded as a raw string literal so no extra
// file needs to be pre-loaded onto SPIFFS.
const char* BUILDER_HTML = R"__BUILDER_HTML__(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>qPocketPCR Protocol Builder (HRM)</title>
<style>
  :root { --line:#d7dbe0; --accent:#2f6fb0; --grey:#f3f4f6; --dark:#333; }
  * { box-sizing: border-box; }
  body { font-family: -apple-system, "Segoe UI", Roboto, "Hiragino Kaku Gothic ProN", sans-serif; margin:0; color:#1b1b1b; background:#fafafa; }
  header { background:#fff; border-bottom:1px solid var(--line); padding:14px 20px; display:flex; align-items:center; gap:12px; }
  header h1 { font-size:18px; margin:0; }
  header a { color:var(--accent); text-decoration:none; font-size:13px; }
  .wrap { max-width:960px; margin:0 auto; padding:20px; }
  section { background:#fff; border:1px solid var(--line); border-radius:10px; padding:16px 18px; margin-bottom:18px; }
  h2 { font-size:15px; margin:0 0 12px; color:#222; }
  .grid { display:grid; grid-template-columns:repeat(2,1fr); gap:12px; }
  label { display:block; font-size:12px; color:#555; margin-bottom:4px; }
  input[type=text], input[type=number], select { width:100%; padding:7px 8px; border:1px solid #b9c0c8; border-radius:6px; font-size:14px; background:#fff; }
  .step { border:1px solid var(--line); border-radius:8px; padding:10px 12px; margin-bottom:10px; position:relative; }
  .step .row { display:flex; gap:8px; align-items:center; flex-wrap:wrap; }
  .step .row > div { flex:1 1 auto; min-width:90px; }
  .step .row .dur { flex:0 0 70px; }
  .step .row .unit { flex:0 0 80px; }
  .step .row .cap { flex:0 0 auto; display:flex; align-items:center; gap:5px; }
  .delbtn { position:absolute; top:8px; right:8px; border:none; background:#e9edf1; color:#555; border-radius:6px; padding:3px 9px; cursor:pointer; font-size:12px; }
  .delbtn:hover { background:#dcdfe4; }
  button.primary { background:var(--accent); color:#fff; border:none; padding:9px 16px; border-radius:8px; font-size:14px; cursor:pointer; }
  button.ghost { background:#fff; color:var(--accent); border:1px solid var(--accent); padding:9px 16px; border-radius:8px; font-size:14px; cursor:pointer; }
  .actions { display:flex; gap:10px; flex-wrap:wrap; align-items:center; margin-top:6px; }
  pre.preview { background:#0f172a; color:#e2e8f0; padding:14px; border-radius:8px; font-size:12.5px; line-height:1.45; overflow:auto; max-height:340px; white-space:pre-wrap; word-break:break-all; }
  .hrm { background:#fff; border-left:1px solid var(--line); }
  /* Toggle-driven section emphasis */
  section { transition:opacity .25s ease, filter .25s ease, box-shadow .25s ease; }
  #ampSection.active { box-shadow:0 0 0 1px var(--accent); }
  #meltSection.active { box-shadow:0 0 0 1px var(--accent); }
  section.dimmed { opacity:.45; filter:saturate(.3); }
  .warn { color:#b45309; font-size:12px; margin-top:6px; min-height:16px; }
  .status { font-size:12px; color:#15803d; min-height:16px; }
  .hint { font-size:12px; color:#666; margin:6px 0 0; }
  .device { display:flex; gap:8px; align-items:center; }
  .chip { display:inline-block; background:var(--grey); border-radius:999px; padding:2px 10px; font-size:12px; color:#444; }
</style>
</head>
<body>
<header>
  <h1>qPocketPCR Protocol Builder</h1>
  <a href="/">&#8592; Back to Home</a>
</header>

<div class="wrap">

  <!-- Header fields -->
  <section>
    <h2>Protocol</h2>
    <div class="grid">
      <div><label>Protocol Name</label><input type="text" id="name" value="My Protocol"></div>
      <div><label>Date</label><input type="date" id="date"></div>
    </div>
    <div class="actions" style="margin-top:10px;">
      <label style="display:flex;align-items:center;gap:6px;margin:0;"><input type="checkbox" id="ampToggle" checked> PCR amplification (REPEAT/CYCLES + steps)</label>
      <label style="display:flex;align-items:center;gap:6px;margin:0;"><input type="checkbox" id="meltToggle" checked> Melting curve (HRM/MELT)</label>
    </div>
  </section>

  <!-- Steps -->
  <section id="ampSection">
    <h2>PCR Block&nbsp;<span class="chip" id="stepCount">0 steps</span></h2>
    <div id="profileChart" style="min-height:140px; border:1px solid var(--line); border-radius:8px; margin-bottom:12px; background:#fff;"></div>

    <!-- PCR cycling parameters -->
    <div class="grid" style="grid-template-columns:repeat(3,1fr); margin-bottom:12px;">
      <div><label>Total Cycles</label><input type="number" id="cycles" value="35" min="0"></div>
      <div><label>Repeat From Step</label><input type="number" id="repeatStart" value="2" min="1"></div>
      <div><label>Repeat To Step</label><input type="number" id="repeatEnd" value="4" min="1"></div>
    </div>

    <div id="steps"></div>

    <button class="ghost" id="addStep">+ Add Step</button>
  </section>

  <!-- HRM / MELT block -->
  <section class="hrm" id="meltSection">
    <h2>Melt (HRM) Block &nbsp;<span class="chip" id="meltPoints">0 points</span></h2>
    <p style="font-size:12px;color:#555;margin:0 0 10px;">Runs a fine temperature ramp with fluorescence capture at each point. Set CYCLES to 0 for an HRM-only run.</p>
    <div class="grid">
      <div><label>MELT FROM (°C)</label><input type="number" id="meltFrom" value="65" step="0.1"></div>
      <div><label>MELT TO (°C)</label><input type="number" id="meltTo" value="95" step="0.1"></div>
      <div><label>MELT INC (°C)</label><input type="number" id="meltInc" value="0.2" step="0.01"></div>
      <div><label>MELT HOLD (sec)</label><input type="number" id="meltHold" value="2" min="1"></div>
    </div>
    <div class="warn" id="meltWarn"></div>
  </section>

  <!-- Actions -->
  <section>
    <h2>Save Protocol</h2>
    <div class="actions">
      <button class="primary" id="saveBtn">Save PROTOCOL.TXT to device</button>
      <button class="ghost" id="downloadBtn">Download</button>
      <span class="status" id="uploadStatus"></span>
    </div>
    <p class="hint">Saves to the device's internal USB storage as <b>PROTOCOL.TXT</b> and makes it the active protocol.</p>
  </section>

  <!-- Named protocol library -->
  <section>
    <h2>Saved Protocols</h2>
    <p class="hint" style="margin:0 0 10px;">Store this protocol in the device's internal memory with a name, or load a previously saved one into the editor.</p>
    <div class="actions">
      <input type="text" id="namedName" placeholder="Protocol name" style="flex:1 1 auto; max-width:220px;" value="">
      <button class="primary" id="saveNamedBtn">Save to device</button>
    </div>
    <span class="status" id="namedStatus"></span>

    <div style="margin-top:12px; border-top:1px solid var(--line); padding-top:12px;">
      <div class="actions">
        <select id="protoList" style="flex:1 1 auto; max-width:300px;"></select>
        <button class="ghost" id="refreshListBtn">Refresh</button>
        <button class="primary" id="loadNamedBtn">Load</button>
        <button class="ghost" id="deleteNamedBtn">Delete</button>
      </div>
      <span class="status" id="loadStatus"></span>
    </div>
  </section>

  <!-- Preview -->
  <section>
    <h2>Protocol Preview</h2>
    <pre class="preview" id="preview"></pre>
  </section>

</div>

<script>
(function(){
  "use strict";

  // ---------- state ----------
  var steps = [
    { name:"Initial step", temp:95, dur:12, unit:"min", capture:false },
    { name:"Denaturation", temp:94, dur:20, unit:"sec", capture:false },
    { name:"Annealing",    temp:65, dur:15, unit:"sec", capture:false },
    { name:"Extension",    temp:72, dur:45, unit:"sec", capture:true  },
    { name:"Final Step",   temp:20, dur:10, unit:"min", capture:false }
  ];

  // ---------- helpers ----------
  function fmtDuration(d, u){ return d + " " + u; }

  function buildProtocolText(){
    var name = document.getElementById("name").value || "Protocol";
    var dateEl = document.getElementById("date");
    var dateStr = dateEl.value ? formatDate(new Date(dateEl.value)) : "";
    var cycles = parseInt(document.getElementById("cycles").value,10) || 0;
    var rs = parseInt(document.getElementById("repeatStart").value,10) || 1;
    var re = parseInt(document.getElementById("repeatEnd").value,10) || 1;
    if (re < rs) re = rs;

    // Toggle: PCR amplification block (REPEAT/CYCLES + STEP lines).
    // When off -> HRM-only protocol (no REPEAT/CYCLES/STEP lines at all).
    var ampOn = document.getElementById("ampToggle").checked;
    // Toggle: MELT / HRM ramp. When off -> amplification-only protocol.
    var meltOn = document.getElementById("meltToggle").checked;

    var lines = [];
    lines.push("NAME: " + name);
    if (dateStr) lines.push(" DATE: " + dateStr);
    lines.push("");

    if (ampOn){
      lines.push(" PROTOCOL: ");
      lines.push("  ");
      lines.push(" REPEAT: " + rs + "-" + re);
      lines.push(" CYCLES: " + cycles);
      lines.push("");

      for (var i=0;i<steps.length;i++){
        var s = steps[i];
        lines.push(" STEP " + (i+1) + ": " + s.name);
        lines.push("    TEMPERATURE: " + s.temp + "\u00b0C");
        lines.push("    DURATION: " + fmtDuration(s.dur, s.unit));
        if (s.capture) lines.push("    CAPTURE: yes");
        lines.push("    ");
      }
    }

    // MELT block (HRM ramp). Only emitted when the HRM toggle is on.
    if (meltOn){
      var mf = parseFloat(document.getElementById("meltFrom").value);
      var mt = parseFloat(document.getElementById("meltTo").value);
      var mi = parseFloat(document.getElementById("meltInc").value);
      var mh = parseInt(document.getElementById("meltHold").value,10) || 1;
      if (!isNaN(mf) && !isNaN(mt) && !isNaN(mi) && mi > 0){
        lines.push(" MELT FROM: " + mf.toFixed(1));
        lines.push(" MELT TO: " + mt.toFixed(1));
        lines.push(" MELT INC: " + mi);
        lines.push(" MELT HOLD: " + mh);
      }
    }

    return lines.join("\n") + "\n";
  }

  function formatDate(d){
    if (isNaN(d)) return "";
    var y=d.getFullYear();
    var m=("0"+(d.getMonth()+1)).slice(-2);
    var day=("0"+d.getDate()).slice(-2);
    return m + "." + day + "." + y;
  }

  function meltPointCount(){
    var mf = parseFloat(document.getElementById("meltFrom").value);
    var mt = parseFloat(document.getElementById("meltTo").value);
    var mi = parseFloat(document.getElementById("meltInc").value);
    if (isNaN(mf)||isNaN(mt)||isNaN(mi)||mi<=0) return 0;
    // Must match Parsing.cpp exactly: (int)((meltTo - meltFrom) / meltInc + 0.5f) + 1
    // Use Math.floor(x+0.5), NOT Math.round, to mirror C++'s truncating cast.
    return Math.floor((mt-mf)/mi + 0.5) + 1;
  }

  // ---------- rendering: step editor ----------
  function renderSteps(){
    var box = document.getElementById("steps");
    box.innerHTML = "";
    steps.forEach(function(s, idx){
      var div = document.createElement("div");
      div.className = "step";
      div.innerHTML =
        '<button class="delbtn" data-idx="'+idx+'">Delete</button>'+
        '<div class="row">'+
          '<div style="flex:2 1 140px;"><label>Step Name</label><input type="text" data-f="name" value="'+esc(s.name)+'"></div>'+
          '<div style="flex:1 1 80px;"><label>Temperature (\u00b0C)</label><input type="number" step="0.1" data-f="temp" value="'+s.temp+'"></div>'+
          '<div class="dur"><label>Duration</label><input type="number" data-f="dur" value="'+s.dur+'"></div>'+
          '<div class="unit"><label>Unit</label><select data-f="unit">'+
            '<option value="sec"'+(s.unit==="sec"?" selected":"")+'>seconds</option>'+
            '<option value="min"'+(s.unit==="min"?" selected":"")+'>minutes</option>'+
          '</select></div>'+
          '<div class="cap"><label style="margin:0;display:flex;align-items:center;height:32px;"><input type="checkbox" data-f="capture"'+(s.capture?" checked":"")+'> Capture</label></div>'+
        '</div>';
      box.appendChild(div);
    });
    document.getElementById("stepCount").textContent = steps.length + " step" + (steps.length===1?"":"s");

    // events
    box.querySelectorAll("input[data-f], select[data-f]").forEach(function(el){
      el.addEventListener("input", function(){
        var stepEl = el.closest(".step");
        var i = Array.prototype.indexOf.call(box.children, stepEl);
        var f = el.getAttribute("data-f");
        if (f==="name"){ steps[i].name = el.value; }
        else if (f==="temp"){ steps[i].temp = parseFloat(el.value)||0; }
        else if (f==="dur"){ steps[i].dur = parseFloat(el.value)||0; }
        else if (f==="unit"){ steps[i].unit = el.value; }
        else if (f==="capture"){ steps[i].capture = el.checked; }
        renderChart(); updatePreview();
      });
    });
    box.querySelectorAll(".delbtn").forEach(function(b){
      b.addEventListener("click", function(){
        var stepEl = b.closest(".step");
        var i = Array.prototype.indexOf.call(box.children, stepEl);
        steps.splice(i,1); renderSteps(); updatePreview();
      });
    });
  }

  function esc(v){ return String(v).replace(/&/g,"&amp;").replace(/"/g,"&quot;"); }

  // ---------- rendering: temperature profile chart ----------
  function renderChart(){
    var c = document.getElementById("profileChart");
    c.innerHTML = "";
    var n = steps.length;
    if (!n){ c.textContent="Add a step to preview the temperature profile."; return; }

    // Fixed logical canvas (matches original qPocketPCR builder look).
    var svgW = 840, svgH = 360;
    var margin = { top:60, right:40, bottom:65, left:65 };
    var plotW = svgW - margin.left - margin.right;
    var plotH = svgH - margin.top - margin.bottom;

    // Responsive sizing: fill container width, keep 840:360 aspect ratio.
    var W = c.clientWidth || (c.parentElement.clientWidth - 40);
    if (W < 200) W = 320;
    var H = Math.round(W * svgH / svgW);

    // Fixed temperature scale 0..105 C (absolute, like a thermal cycler).
    var minT = 0, maxT = 105;
    function getY(t){
      var cl = Math.max(minT, Math.min(maxT, t));
      return margin.top + plotH - ((cl - minT)/(maxT - minT)) * plotH;
    }

    var svgNS = "http://www.w3.org/2000/svg";
    var svg = document.createElementNS(svgNS, "svg");
    svg.setAttribute("viewBox", "0 0 " + svgW + " " + svgH);
    svg.style.width = W + "px"; svg.style.height = H + "px";

    // Background panel
    var bg = document.createElementNS(svgNS, "rect");
    bg.setAttribute("width", svgW); bg.setAttribute("height", svgH); bg.setAttribute("fill", "#ffffff"); bg.setAttribute("rx", 12);
    svg.appendChild(bg);

    // Title: Protocol: [name]   Date: [date]
    var name = document.getElementById("name").value || "Protocol name";
    var dateEl = document.getElementById("date");
    var dateStr = dateEl.value ? formatDate(new Date(dateEl.value)) : "";
    var title = document.createElementNS(svgNS, "text");
    title.setAttribute("x", svgW/2); title.setAttribute("y", 30);
    title.setAttribute("text-anchor", "middle");
    title.setAttribute("font-size", "14"); title.setAttribute("font-weight", "600"); title.setAttribute("fill", "#334155");
    title.textContent = "Protocol: " + name + "   Date: " + dateStr;
    svg.appendChild(title);

    // Horizontal gridlines + Y-axis temperature labels (0,20,...,100)
    var yTicks = [0,20,40,60,80,100];
    for (var t=0; t<yTicks.length; t++){
      var gy = getY(yTicks[t]);
      var gl = document.createElementNS(svgNS, "line");
      gl.setAttribute("x1", margin.left); gl.setAttribute("y1", gy);
      gl.setAttribute("x2", svgW - margin.right); gl.setAttribute("y2", gy);
      gl.setAttribute("stroke", "#f1f5f9"); gl.setAttribute("stroke-width", "1.5");
      svg.appendChild(gl);
      var lt = document.createElementNS(svgNS, "text");
      lt.setAttribute("x", margin.left - 10); lt.setAttribute("y", gy + 4);
      lt.setAttribute("text-anchor", "end"); lt.setAttribute("font-size", "11");
      lt.setAttribute("fill", "#94a3b8");
      lt.textContent = yTicks[t] + "\u00b0C";
      svg.appendChild(lt);
    }

    // Step slots (each step occupies one slot across the plot width).
    var stepCount = Math.max(1, n);
    var stepW = plotW / stepCount;
    function xFor(i){ return margin.left + i * stepW; }

    // Baseline (pre-run room temp ~20C) then staircase through each step.
    var pathD = "M " + (margin.left - 25) + " " + getY(20) + " L " + margin.left + " " + getY(20);
    for (var i=0; i<n; i++){
      var xS = xFor(i), xE = xFor(i+1);
      var y = getY(steps[i].temp);
      pathD += " L " + xS + " " + y;   // vertical rise to set temp
      pathD += " L " + xE + " " + y;   // horizontal hold
    }
    var profile = document.createElementNS(svgNS, "path");
    profile.setAttribute("d", pathD);
    profile.setAttribute("fill", "none");
    profile.setAttribute("stroke", "#7c3aed");
    profile.setAttribute("stroke-width", "3");
    profile.setAttribute("stroke-linejoin", "round");
    profile.setAttribute("stroke-linecap", "round");
    svg.appendChild(profile);

    // Per-step guides + labels
    for (var i=0; i<n; i++){
      var xS = xFor(i), xE = xFor(i+1);
      var y = getY(steps[i].temp);
      var xc = (xS + xE) / 2;

      // vertical guide between steps
      if (i > 0){
        var gd = document.createElementNS(svgNS, "line");
        gd.setAttribute("x1", xS); gd.setAttribute("y1", margin.top);
        gd.setAttribute("x2", xS); gd.setAttribute("y2", margin.top + plotH);
        gd.setAttribute("stroke", "#e2e8f0"); gd.setAttribute("stroke-width", "1");
        gd.setAttribute("stroke-dasharray", "3,3");
        svg.appendChild(gd);
      }

      // step number (top)
      var sn = document.createElementNS(svgNS, "text");
      sn.setAttribute("x", xc); sn.setAttribute("y", margin.top - 12);
      sn.setAttribute("text-anchor", "middle"); sn.setAttribute("font-size", "13");
      sn.setAttribute("font-weight", "700"); sn.setAttribute("fill", "#475569");
      sn.textContent = (i+1);
      svg.appendChild(sn);

      // temperature label (above line)
      var tt = document.createElementNS(svgNS, "text");
      tt.setAttribute("x", xc); tt.setAttribute("y", y - 10);
      tt.setAttribute("text-anchor", "middle"); tt.setAttribute("font-size", "12");
      tt.setAttribute("font-weight", "700"); tt.setAttribute("fill", "#1e293b");
      tt.textContent = steps[i].temp + "\u00b0C";
      svg.appendChild(tt);

      // capture marker (double-ring target) below line when on
      if (steps[i].capture){
        var cg = document.createElementNS(svgNS, "g");
        cg.setAttribute("transform", "translate(" + xc + ", " + (y + 24) + ")");
        var c1 = document.createElementNS(svgNS, "circle");
        c1.setAttribute("cx", 0); c1.setAttribute("cy", 0); c1.setAttribute("r", 10);
        c1.setAttribute("fill", "#ffffff"); c1.setAttribute("stroke", "#475569"); c1.setAttribute("stroke-width", "2");
        cg.appendChild(c1);
        var c2 = document.createElementNS(svgNS, "circle");
        c2.setAttribute("cx", 0); c2.setAttribute("cy", 0); c2.setAttribute("r", 5);
        c2.setAttribute("fill", "#334155");
        cg.appendChild(c2);
        var c3 = document.createElementNS(svgNS, "circle");
        c3.setAttribute("cx", 0); c3.setAttribute("cy", 0); c3.setAttribute("r", 2);
        c3.setAttribute("fill", "#ffffff");
        cg.appendChild(c3);
        svg.appendChild(cg);
      }

      // duration label (bottom)
      var du = steps[i].unit === "min" ? "min" : "sec";
      var dt = document.createElementNS(svgNS, "text");
      dt.setAttribute("x", xc); dt.setAttribute("y", svgH - margin.bottom + 42);
      dt.setAttribute("text-anchor", "middle"); dt.setAttribute("font-size", "11");
      dt.setAttribute("font-weight", "500"); dt.setAttribute("fill", "#64748b");
      dt.textContent = steps[i].dur + " " + du;
      svg.appendChild(dt);
    }

    // Repeat bar (bottom) showing cycle count over the repeat range.
    var rs = parseInt(document.getElementById("repeatStart").value,10) || 1;
    var re = parseInt(document.getElementById("repeatEnd").value,10) || 1;
    var cycles = parseInt(document.getElementById("cycles").value,10) || 0;
    var ampOn = document.getElementById("ampToggle").checked;
    if (ampOn && n > 1){
      var rFrom = Math.max(1, rs), rTo = Math.min(n, re);
      if (rFrom <= rTo){
        var rxS = xFor(rFrom - 1) + 2;
        var rxE = xFor(rTo) - 2;
        if (rxE > rxS){
          var barY = svgH - margin.bottom + 10;
          var br = document.createElementNS(svgNS, "rect");
          br.setAttribute("x", rxS); br.setAttribute("y", barY);
          br.setAttribute("width", rxE - rxS); br.setAttribute("height", 14);
          br.setAttribute("rx", 4); br.setAttribute("fill", "#a855f7");
          svg.appendChild(br);
          var bl = document.createElementNS(svgNS, "text");
          bl.setAttribute("x", (rxS + rxE)/2); bl.setAttribute("y", barY + 11);
          bl.setAttribute("text-anchor", "middle"); bl.setAttribute("font-size", "11");
          bl.setAttribute("font-weight", "700"); bl.setAttribute("fill", "#ffffff");
          bl.textContent = cycles + "\u00d7";
          svg.appendChild(bl);
        }
      }
    }

    c.appendChild(svg);
  }

  // Firmware limit for total protocol steps / capture measurements.
  // Kept in sync with MAX_STEPS (Parsing.h) and MAX_MEASUREMENTS (main).
  const MAX_STEPS_LIMIT = 512;

  // ---------- preview + melt warn ----------
  function updatePreview(){
    document.getElementById("preview").textContent = buildProtocolText();
    var mp = meltPointCount();
    var ampOn = document.getElementById("ampToggle").checked;
    document.getElementById("meltPoints").textContent = mp + " point" + (mp===1?"":"s");
    var totalSteps = steps.length + mp;
    var warn = document.getElementById("meltWarn");
    if (!ampOn){
      warn.textContent = "PCR amplification off \u2192 HRM-only run (CYCLES/MELT execute once).";
    } else if (totalSteps > MAX_STEPS_LIMIT){
      warn.textContent = "\u26a0 Total steps ("+totalSteps+") exceed MAX_STEPS="+MAX_STEPS_LIMIT+". Melt ramp will be truncated to "+(MAX_STEPS_LIMIT-steps.length)+" points.";
    } else {
      warn.textContent = "";
    }
  }

  // ---------- protocol text -> form ----------
  // Returns the trimmed value that follows "KEY:" on the first matching line.
  function lineValue(lines, key){
    var k = key.toUpperCase();
    for (var i=0;i<lines.length;i++){
      var t = lines[i].replace(/^\s+/,"");
      if (t.toUpperCase().indexOf(k) === 0) return t.substring(key.length).trim();
    }
    return "";
  }

  // "mm.dd.yyyy" -> "yyyy-mm-dd" (for <input type=date>)
  function parseDateToInput(s){
    if (!s) return "";
    var m = s.match(/(\d{1,2})[.\/](\d{1,2})[.\/](\d{2,4})/);
    if (!m) return "";
    var yy = m[3]; if (yy.length === 2) yy = "20" + yy;
    return yy + "-" + ("0"+m[1]).slice(-2) + "-" + ("0"+m[2]).slice(-2);
  }

  function populateFromProtocolText(text){
    if (!text || !text.trim()) return false;
    var lines = text.replace(/\r/g,"").split("\n");

    var name = lineValue(lines, "NAME:");
    if (name) document.getElementById("name").value = name;

    var di = parseDateToInput(lineValue(lines, "DATE:"));
    if (di) document.getElementById("date").value = di;

    var repeat = lineValue(lines, "REPEAT:");
    if (repeat && repeat.indexOf("-") >= 0){
      var parts = repeat.split("-");
      var a = parseInt(parts[0],10), b = parseInt(parts[1],10);
      if (!isNaN(a)) document.getElementById("repeatStart").value = a;
      if (!isNaN(b)) document.getElementById("repeatEnd").value = b;
    }
    var cycStr = lineValue(lines, "CYCLES:");
    if (cycStr !== ""){ var c = parseInt(cycStr,10); if (!isNaN(c)) document.getElementById("cycles").value = c; }

    // Steps: a STEP line starts a block; the following lines carry its fields.
    var newSteps = [];
    var stepRe = /^\s*STEP\s+\d+\s*:\s*(.*)$/i;
    var i = 0;
    while (i < lines.length){
      var sm = lines[i].match(stepRe);
      if (sm){
        var st = { name: sm[1].trim(), temp: 0, dur: 0, unit:"sec", capture:false };
        i++;
        while (i < lines.length && !stepRe.test(lines[i])){
          var l = lines[i];
          var tm = l.match(/TEMPERATURE\s*:\s*([\-\d.]+)/i);
          if (tm) st.temp = parseFloat(tm[1]) || 0;
          var dm = l.match(/DURATION\s*:\s*([\d.]+)\s*([a-zA-Z]*)/i);
          if (dm){
            st.dur = parseFloat(dm[1]) || 0;
            st.unit = /m/i.test(dm[2]) ? "min" : "sec";
          }
          if (/CAPTURE\s*:/i.test(l)) st.capture = true;
          i++;
        }
        newSteps.push(st);
        continue;
      }
      i++;
    }

    var hasMelt = /(^|\n)\s*MELT\s+(FROM|TO|INC|HOLD)\s*:/i.test(text);
    var ampOn = (newSteps.length > 0) || (repeat !== "") || (cycStr !== "");

    // HRM-only protocol: keep the step editor empty.
    steps = ampOn ? newSteps : [];
    document.getElementById("ampToggle").checked = ampOn;
    document.getElementById("meltToggle").checked = hasMelt;

    if (hasMelt){
      var mf = lineValue(lines,"MELT FROM:"); if (mf !== "") document.getElementById("meltFrom").value = mf;
      var mt = lineValue(lines,"MELT TO:");   if (mt !== "") document.getElementById("meltTo").value = mt;
      var mi = lineValue(lines,"MELT INC:");  if (mi !== "") document.getElementById("meltInc").value = mi;
      var mh = lineValue(lines,"MELT HOLD:"); if (mh !== "") document.getElementById("meltHold").value = mh;
    }

    renderSteps();
    renderChart();
    updatePreview();
    refreshSectionStates();
    return true;
  }

  // ---------- save / load ----------
  function downloadProtocol(){
    var text = buildProtocolText();
    var blob = new Blob([text], {type:"text/plain"});
    var a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = "PROTOCOL.TXT";
    document.body.appendChild(a); a.click();
    setTimeout(function(){ URL.revokeObjectURL(a.href); a.remove(); }, 0);
  }

  function saveProtocolToDevice(){
    var status = document.getElementById("uploadStatus");
    var text = buildProtocolText();
    status.textContent = "Saving to device...";
    fetch("/saveprotocol", { method:"POST", headers:{"Content-Type":"text/plain"}, body:text })
      .then(function(r){ return r.text().then(function(t){ return {ok:r.ok, t:t}; }); })
      .then(function(res){
        status.textContent = res.ok
          ? "Saved to device USB storage (PROTOCOL.TXT)"
          : ("Save failed: " + res.t);
      })
      .catch(function(e){ status.textContent = "Save error: " + e.message; });
  }

  function saveNamedToDevice(){
    var name = document.getElementById("namedName").value.trim() || "Unnamed";
    var status = document.getElementById("namedStatus");
    var text = buildProtocolText();
    if (!text){ status.textContent = "Add a step first."; return; }
    status.textContent = "Saving...";
    // Send the protocol as a raw text body and the name as a query parameter.
    // (multipart form-data is not parsed without an upload handler on the device)
    fetch("/saveproto?name=" + encodeURIComponent(name), {
        method:"POST",
        headers:{"Content-Type":"text/plain"},
        body:text
      })
      .then(function(r){ return r.text().then(function(t){ return {ok:r.ok, t:t}; }); })
      .then(function(res){
        status.textContent = res.ok ? ("Saved \"" + name + "\" to device memory") : ("Save failed: " + res.t);
        loadProtocolList();
      })
      .catch(function(e){ status.textContent = "Save error: " + e.message; });
  }

  // ---------- saved protocol library ----------
  function loadProtocolList(){
    var sel = document.getElementById("protoList");
    if (!sel) return;
    fetch("/listproto")
      .then(function(r){ return r.text(); })
      .then(function(t){
        sel.innerHTML = "";
        var any = false;
        t.replace(/\r/g,"").split("\n").forEach(function(line){
          line = line.trim();
          if (!line || line.indexOf("|") < 0) return;
          var p = line.split("|");
          var o = document.createElement("option");
          o.value = p[0];
          o.textContent = p[0] + ": " + p.slice(1).join("|");
          sel.appendChild(o);
          any = true;
        });
        if (!any){
          var o = document.createElement("option");
          o.value = ""; o.textContent = "(no saved protocols)";
          sel.appendChild(o);
        }
      })
      .catch(function(e){});
  }

  function loadSelectedProtocol(){
    var sel = document.getElementById("protoList");
    var id = sel ? sel.value : "";
    var status = document.getElementById("loadStatus");
    if (!id){ status.textContent = "No protocol selected."; return; }
    status.textContent = "Loading...";
    fetch("/loadproto?id=" + encodeURIComponent(id))
      .then(function(r){ return r.text().then(function(t){ return {ok:r.ok, t:t}; }); })
      .then(function(res){
        if (!res.ok){ status.textContent = "Load failed: " + res.t; return; }
        if (populateFromProtocolText(res.t)){
          status.textContent = "Loaded into the editor. Press \"Save PROTOCOL.TXT to device\" to activate it.";
        } else {
          status.textContent = "Loaded, but the content could not be parsed.";
        }
      })
      .catch(function(e){ status.textContent = "Load error: " + e.message; });
  }

  function deleteSelectedProtocol(){
    var sel = document.getElementById("protoList");
    var id = sel ? sel.value : "";
    var status = document.getElementById("loadStatus");
    if (!id){ status.textContent = "No protocol selected."; return; }
    var label = (sel.selectedIndex >= 0 && sel.options[sel.selectedIndex])
      ? sel.options[sel.selectedIndex].textContent : id;
    if (!window.confirm("Delete saved protocol \"" + label + "\"?")) return;
    status.textContent = "Deleting...";
    fetch("/deleteproto?id=" + encodeURIComponent(id), { method:"POST" })
      .then(function(r){ return r.text().then(function(t){ return {ok:r.ok, t:t}; }); })
      .then(function(res){
        status.textContent = res.ok ? "Deleted." : ("Delete failed: " + res.t);
        loadProtocolList();
      })
      .catch(function(e){ status.textContent = "Delete error: " + e.message; });
  }

  // Reflect the current active protocol into the editor on open.
  function loadCurrentProtocol(){
    fetch("/getproto")
      .then(function(r){ return r.ok ? r.text() : ""; })
      .then(function(t){ if (t && t.trim()) populateFromProtocolText(t); })
      .catch(function(e){});
  }

  // ---------- init ----------
  document.getElementById("addStep").addEventListener("click", function(){
    steps.push({ name:"New step", temp:72, dur:30, unit:"sec", capture:false });
    renderSteps(); updatePreview();
  });
  document.getElementById("saveBtn").addEventListener("click", saveProtocolToDevice);
  document.getElementById("downloadBtn").addEventListener("click", downloadProtocol);
  document.getElementById("saveNamedBtn").addEventListener("click", saveNamedToDevice);
  document.getElementById("loadNamedBtn").addEventListener("click", loadSelectedProtocol);
  document.getElementById("deleteNamedBtn").addEventListener("click", deleteSelectedProtocol);
  document.getElementById("refreshListBtn").addEventListener("click", loadProtocolList);

  // live-update when melt fields change too
  ["meltFrom","meltTo","meltInc","meltHold"].forEach(function(id){
    document.getElementById(id).addEventListener("input", updatePreview);
  });

  // Toggle-driven section emphasis: active block highlighted, inactive dimmed.
  function refreshSectionStates(){
    var ampOn = document.getElementById("ampToggle").checked;
    var meltOn = document.getElementById("meltToggle").checked;
    var ampSec = document.getElementById("ampSection");
    var meltSec = document.getElementById("meltSection");
    if (ampOn){ ampSec.classList.add("active"); ampSec.classList.remove("dimmed"); } else { ampSec.classList.remove("active"); ampSec.classList.add("dimmed"); }
    if (meltOn){ meltSec.classList.add("active"); meltSec.classList.remove("dimmed"); } else { meltSec.classList.remove("active"); meltSec.classList.add("dimmed"); }
  }

  // Re-render chart/preview when the PCR / HRM toggles change.
  ["ampToggle","meltToggle"].forEach(function(id){
    document.getElementById(id).addEventListener("change", function(){ renderChart(); updatePreview(); refreshSectionStates(); });
  });

  // default date = today
  var d = new Date();
  document.getElementById("date").value = formatDate(d);

  renderSteps();
  renderChart();
  updatePreview();
  refreshSectionStates();
  window.addEventListener("resize", renderChart);

  // Pull the current protocol and the saved-protocol list from the device.
  loadCurrentProtocol();
  loadProtocolList();
})();
</script>
<footer style="margin-top:2em;font-size:11px;color:#888;">qPocketPCR-wHRM v0.24 &mdash; forked from qPocketPCR V1.1 (GaudiLabs); &copy; Takakura</footer>
</body>
</html>
)__BUILDER_HTML__";

void handleBuilder() {
  server.send(200, "text/html", BUILDER_HTML);
}

// Top page (served by handleRoot). Same design as the Protocol Builder page.
// Values are injected by handleRoot() via the %...% placeholders below.
const char* INDEX_HTML = R"__INDEX_HTML__(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>qPocketPCR</title>
<style>
  :root { --line:#d7dbe0; --accent:#2f6fb0; --accent-dk:#255a91; --grey:#f3f4f6; --dark:#333; --muted:#666; }
  * { box-sizing: border-box; }
  body {
    font-family: -apple-system, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif;
    margin: 0; color: #1b1b1b; background: #fafafa;
    -webkit-text-size-adjust: 100%;
  }

  /* ---------- chrome (same as /builder) ---------- */
  header { background:#fff; border-bottom:1px solid var(--line); padding:14px 20px; display:flex; align-items:center; gap:12px; flex-wrap:wrap; }
  header h1 { font-size:18px; margin:0; }
  header .spacer { flex:1 1 auto; }
  header a {
    background: var(--accent);
    color: #fff;
    text-decoration: none;
    font-size: 13px;
    font-weight: 600;
    padding: 8px 16px;
    border-radius: 6px;
    white-space: nowrap;
    box-shadow: 0 2px 4px rgba(47,111,176,0.2);
    transition: background 0.2s ease;
  }
  header a:hover {
    background: var(--accent-dk);
    text-decoration: none;
  }
  .wrap { max-width:960px; margin:0 auto; padding:20px; }
  section { background:#fff; border:1px solid var(--line); border-radius:10px; padding:16px 18px; margin-bottom:18px; }
  h2 { font-size:15px; margin:0 0 12px; color:#222; }
  .credit { margin:2em 0 0; font-size:11px; color:#888; }

  /* ---------- controls ---------- */
  button, a.btn { font:inherit; font-size:14px; border-radius:8px; padding:9px 16px; cursor:pointer; }
  a.btn { display:inline-block; text-decoration:none; }
  .primary { background:var(--accent); color:#fff; border:1px solid var(--accent); }
  .primary:hover { background:var(--accent-dk); border-color:var(--accent-dk); }
  .ghost { background:#fff; color:var(--accent); border:1px solid var(--accent); }
  .ghost:hover { background:#f2f7fc; }
  .actions { display:flex; gap:10px; flex-wrap:wrap; align-items:center; }
  .hint { font-size:12px; color:var(--muted); margin:8px 0 0; }

  /* ---------- status ---------- */
  .tiles { display:grid; grid-template-columns:repeat(auto-fit,minmax(140px,1fr)); gap:12px; }
  .tile { background:var(--grey); border-radius:8px; padding:10px 12px; min-width:0; }
  .tile .k { font-size:11px; letter-spacing:.04em; text-transform:uppercase; color:var(--muted); }
  .tile .v { font-size:22px; font-weight:600; line-height:1.25; margin-top:2px; word-break:break-word; }
  .tile .v small { font-size:13px; font-weight:400; color:var(--muted); }
  .bar { height:6px; background:#e3e7eb; border-radius:999px; margin-top:14px; overflow:hidden; }
  .bar > i { display:block; height:100%; background:var(--accent); border-radius:999px; }

  /* ---------- misc ---------- */
  input[type=file] { font-size:13px; color:#333; }
  .note { font-size:12px; color:var(--muted); }

  @media (max-width:420px) {
    .wrap { padding:14px; }
    section { padding:14px; }
    .tile .v { font-size:19px; }
    .actions > button, .actions > a.btn { flex:1 1 auto; text-align:center; }
  }
</style>
</head>
<body>

<header>
  <h1>qPocketPCR</h1>
  <span class="spacer"></span>
  <a href="/builder" target="_blank">Protocol Builder &#8594;</a>
</header>

<div class="wrap">

  <!-- ============ Status (server-rendered, no polling) ============ -->
  <section>
    <h2>Status</h2>
    <div class="tiles">
      <div class="tile">
        <div class="k">Mode</div>
        <div class="v"><span id="mode">%MODE%</span></div>
      </div>
      <div class="tile">
        <div class="k">Progress</div>
        <div class="v"><span id="progress">%PROGRESS%</span><small>%</small></div>
      </div>
      <div class="tile">
        <div class="k">Step</div>
        <div class="v"><span id="step">%STEP%</span></div>
      </div>
      <div class="tile">
        <div class="k">Temp</div>
        <div class="v"><span id="temp">%TEMP%</span><small> &deg;C</small></div>
      </div>
    </div>
    <div class="bar"><i id="progressBar" style="width:%PROGRESS%%"></i></div>
  </section>

  <!-- ============ Control ============ -->
  <section>
    <h2>Control</h2>
    <div class="actions">
      <button class="primary" onclick="fetch('/start')">Start</button>
      <button class="ghost" onclick="fetch('/stop')">Stop</button>
    </div>
  </section>

  <!-- ============ Protocol ============ -->
  <section>
    <h2>Protocol</h2>
    <form action="/upload" method="post" enctype="multipart/form-data" class="actions">
      <input type="file" name="protocol" accept=".txt">
      <button class="primary" type="submit">Upload</button>
    </form>
    <p class="hint">Upload a new <b>PROTOCOL.TXT</b> to make it the active protocol.</p>
  </section>

  <!-- ============ Results ============ -->
  <section>
    <h2>Results</h2>
    <div class="actions">
      <a class="btn ghost" href="/download">Download DATAQPCR.TXT</a>
    </div>
  </section>

  <p class="credit">qPocketPCR-wHRM %VERSION% &mdash; forked from qPocketPCR V1.1 (GaudiLabs); &copy; Takakura</p>

</div>

</body>
</html>
)__INDEX_HTML__";

void handleRoot() {
  String html = INDEX_HTML;
  html.replace("%MODE%", getModeString());
  html.replace("%PROGRESS%", String(getProgress()));
  html.replace("%STEP%", String(PCRstep + 1) + "/" + String(pcrProtocol.stepCount));
  html.replace("%TEMP%", String(temperature_mean, 1));
  html.replace("%VERSION%", VERSION_STRING);
  server.send(200, "text/html", html);
}

void handleStatus() {
  String json = "{";
  json += "\"mode\":\"" + getModeString() + "\",";
  json += "\"progress\":" + String(getProgress()) + ",";
  json += "\"step\":" + String(PCRstep + 1) + ",";
  json += "\"stepCount\":" + String(pcrProtocol.stepCount) + ",";
  json += "\"temp\":" + String(temperature_mean, 1) + ",";
  json += "\"cycle\":" + String(PCRcycle) + ",";
  json += "\"cycleCount\":" + String(pcrProtocol.cycleCount);
  json += "}";
  server.send(200, "application/json", json);
}

void handleStart() {
  if (caseUX == CASE_Main || casePCR == PCR_END) {
    loadProtocol();

    // WiFi経由の開始でも画面RUNと同じバッファ上限チェックを行う。
    // これがないとfluorescence[][]（MAX_MEASUREMENTS=400）をオーバーフローし、
    // 配列外書き出しによるメモリ破壊でESP32がクラッシュ／リセットする。
    // NOTE: 必ずグローバル captures を更新する。ここを更新しないと、後続の
    //       drawGrid() が grid_w / captures を計算する際に captures==0 となり、
    //       ゼロ除算例外でESP32が再起動する（画面RUNではこの代入がある）。
    captures = countCaptures();
    if (captures >= MAX_MEASUREMENTS) {
      Serial.print("WiFi start: Too many measurements: ");
      Serial.println(captures);
      server.send(200, "text/plain", "Too many measurements: " + String(captures));
      return;
    }

    caseUX = CASE_InitQPCR;
    server.send(200, "text/plain", "Started");
  } else {
    server.send(200, "text/plain", "Already running");
  }
}

void handleStop() {
  stopRequested = true;
  server.send(200, "text/plain", "Stop requested");
}

void handleDownload() {
  File file = SPIFFS.open("/DATA.TXT", FILE_READ);
  if (!file) {
    server.send(404, "text/plain", "File not found");
    return;
  }
  // Content-Disposition でファイル名を指定
  server.sendHeader("Content-Disposition", "attachment; filename=DATAQPCR.TXT");
  server.streamFile(file, "text/csv");
  file.close();
}

void handleUpload() {
  HTTPUpload& upload = server.upload();
  static File uploadFile;
  static size_t uploadSize = 0;
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("Upload start: %s\n", upload.name.c_str());
    uploadSize = 0;
    uploadFile = SPIFFS.open("/PROTOCOL.TXT", FILE_WRITE);
    if (!uploadFile) {
      Serial.println("Failed to open file for writing!");
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      size_t written = uploadFile.write(upload.buf, upload.currentSize);
      uploadSize += written;
      Serial.printf("Write: %d bytes (total: %d)\n", written, uploadSize);
    } else {
      Serial.println("File not open for writing!");
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
      Serial.printf("Upload complete: %d bytes total\n", uploadSize);
    } else {
      Serial.println("File was not open!");
    }

    // Mirror the uploaded protocol into the virtual USB drive so the builder
    // and the running protocol agree with what was just uploaded.
    File rf = SPIFFS.open("/PROTOCOL.TXT", FILE_READ);
    if (rf) {
      String text = rf.readString();
      rf.close();
      addProtoToFAT(text);
      saveMscToSPIFFS(msc_disk);
      newConfigAvailable = true;
    }
  }
}

void handleUploadDone() {
  server.send(200, "text/plain", "Upload complete");
}

// --- Named protocol library: return list of saved protocols ---
// Response format: newline-separated "id|name" lines.
void handleListProtocols() {
  String list = listProtocols();
  if (list.length() == 0) {
    server.send(200, "text/plain", "No saved protocols");
    return;
  }
  server.send(200, "text/plain", list);
}

// --- Named protocol library: load one by id and parse it into pcrProtocol ---
// Returns the raw protocol text so the builder UI can repopulate its form.
void handleLoadProtocol() {
  String arg = server.arg("id");
  int id = arg.toInt();
  if (id < 1) {
    server.send(400, "text/plain", "Invalid id");
    return;
  }
  String text;
  if (!loadProtocolById(id, text)) {
    server.send(404, "text/plain", "Protocol not found");
    return;
  }
  server.send(200, "text/plain", text);
}

// --- Named protocol library: save the current builder form as a named protocol ---
void handleSaveProtocol() {
  String name = server.arg("name");
  // Accept a raw text body (arg "plain", preferred) or a multipart field.
  String text = server.arg("plain");
  if (text.length() == 0) text = server.arg("protocol");
  if (text.length() == 0) {
    server.send(400, "text/plain", "No protocol data");
    return;
  }
  saveNamedProtocol(name, text);
  // Return the updated list so the UI can confirm which slot was used.
  String list = listProtocols();
  server.send(200, "text/plain", list);
}

// --- Named protocol library: delete one saved protocol by id ---
void handleDeleteProtocol() {
  int id = server.arg("id").toInt();
  if (id < 1) {
    server.send(400, "text/plain", "Invalid id");
    return;
  }
  if (!deleteProtocolById(id)) {
    server.send(404, "text/plain", "Protocol not found");
    return;
  }
  // Return the updated list so the UI can refresh.
  server.send(200, "text/plain", listProtocols());
}

// --- Active protocol: return the protocol that would run next ---
// Used by the builder to pre-fill the editor with the current protocol.
void handleGetProtocol() {
  // Prefer the PROTOCOL.TXT visible on the virtual USB drive so the builder
  // reflects the file the user actually edits there. Fall back to a protocol
  // stored on SPIFFS (web upload) and finally to the factory template.
  String text = getConfig();
  if (text.length() == 0) {
    File f = SPIFFS.open("/PROTOCOL.TXT", FILE_READ);
    if (f) { text = f.readString(); f.close(); }
  }
  if (text.length() == 0) text = PROTOCOL_TEMPLATE;
  server.send(200, "text/plain", text);
}

// --- Active protocol: store the builder output as PROTOCOL.TXT ---
// Writes it both to SPIFFS (read first by loadProtocol) and to the virtual USB
// drive's PROTOCOL.TXT, then persists the disk image so the host sees it too.
void handleSaveActiveProtocol() {
  String text = server.arg("plain");
  if (text.length() == 0) {
    server.send(400, "text/plain", "No protocol data");
    return;
  }

  File f = SPIFFS.open("/PROTOCOL.TXT", FILE_WRITE);
  if (!f) {
    server.send(500, "text/plain", "SPIFFS write failed");
    return;
  }
  f.print(text);
  f.close();

  // Mirror into the USB FAT image and persist it to SPIFFS.
  addProtoToFAT(text);
  saveMscToSPIFFS(msc_disk);

  // Ask the main loop to re-parse the new active protocol.
  newConfigAvailable = true;

  Serial.printf("handleSaveActiveProtocol: saved %d bytes\n", text.length());
  server.send(200, "text/plain", "Saved");
}
