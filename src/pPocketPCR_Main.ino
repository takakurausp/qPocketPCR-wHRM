// qPocketPCR Software
// by Urs Gaudenz GaudiLabs 2025

// Included Libraries
#include "USB.h"
#include "USBMSC.h"
#include "FS.h"
#include "SPIFFS.h"
#include "USB_DRIVE.h"
#include "Parsing.h"

#include <WiFi.h>
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
#define VERSION_STRING  "v0.1"
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

#define MAX_MEASUREMENTS 400

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
bool stopRequested = false;


// ==================== SETUP ====================

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

  // WiFi AP モード開始
  WiFi.softAP(ap_ssid, ap_password);
  wifiEnabled = true;
  Serial.print("WiFi AP started: ");
  Serial.println(ap_ssid);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());

  // Web サーバーハンドラ登録
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
  server.begin();
  Serial.println("Web server started on port 80");

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
  int xSpacing=grid_w/captures;
  if (xSpacing<minSpacing) {division=ceil(minSpacing*captures/grid_w);xSpacing=division*grid_w/captures;}

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
  for (int i = 0; i <= (captures/division); i++) {
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
const char* BUILDER_HTML = R"__BUILDER_HTML__("<!DOCTYPE html>
<html lang="ja">
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
  .hrm { border-left:4px solid var(--accent); background:#f6f9fc; }
  .warn { color:#b45309; font-size:12px; margin-top:6px; min-height:16px; }
  .status { font-size:12px; color:#15803d; min-height:16px; }
  .device { display:flex; gap:8px; align-items:center; }
  .chip { display:inline-block; background:var(--grey); border-radius:999px; padding:2px 10px; font-size:12px; color:#444; }
</style>
</head>
<body>
<header>
  <h1>qPocketPCR Protocol Builder</h1>
  <a href="/">&#8592; トップへ戻る</a>
</header>

<div class="wrap">

  <!-- Header fields -->
  <section>
    <h2>Protocol</h2>
    <div class="grid">
      <div><label>Protocol Name</label><input type="text" id="name" value="My Protocol"></div>
      <div><label>Date</label><input type="date" id="date"></div>
      <div><label>Total Cycles</label><input type="number" id="cycles" value="35" min="0"></div>
      <div><label>Repeat From Step</label><input type="number" id="repeatStart" value="2" min="1"></div>
      <div><label>Repeat To Step</label><input type="number" id="repeatEnd" value="4" min="1"></div>
    </div>
  </section>

  <!-- Steps -->
  <section>
    <h2>Protocol Temperature Profile &nbsp;<span class="chip" id="stepCount">0 steps</span></h2>
    <div id="profileChart" style="height:150px; border:1px solid var(--line); border-radius:8px; margin-bottom:12px; background:#fff;"></div>

    <div id="steps"></div>

    <button class="ghost" id="addStep">+ Add Step</button>
  </section>

  <!-- HRM / MELT block -->
  <section class="hrm">
    <h2>Melt (HRM) Block &nbsp;<span class="chip" id="meltPoints">0 points</span></h2>
    <p style="font-size:12px;color:#555;margin:0 0 10px;">PCR後に細かな温度ランプを行い、各ポイントで蛍光を取得します。CYCLES:0 にすると MELT のみ実行できます。</p>
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
    <h2>Save &amp; Upload</h2>
    <div class="actions">
      <button class="primary" id="saveBtn">Save Protocol as PROTOCOL.TXT</button>
      <span class="status" id="uploadStatus"></span>
    </div>
    <div style="margin-top:12px;" class="device">
      <label style="margin:0;">Upload to device:</label>
      <input type="text" id="deviceUrl" placeholder="http://192.168.4.1/" style="max-width:200px;" value="">
      <span class="chip" id="uploadHint">SAVE は PROTOCOL.TXT をダウンロードします</span>
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

    var lines = [];
    lines.push("NAME: " + name);
    if (dateStr) lines.push(" DATE: " + dateStr);
    lines.push("");
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

    // MELT block
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
        var s = steps[+el.getAttribute("data-idx") || 0];
        // find step by index attribute on closest .step
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
    var W = c.clientWidth || (c.parentElement.clientWidth - 40);
    if (W < 100) W = 300;
    var H = 150, padL=28, padB=16;
    var n = steps.length;
    if (!n){ c.textContent="Add a step to preview the temperature profile."; return; }
    var temps = steps.map(function(s){return s.temp;});
    var tmin = Math.min.apply(null,temps), tmax = Math.max.apply(null,temps);
    var span = (tmax-tmin)||10; tmin-=span*0.15; tmax+=span*0.15;
    var usableW = W - padL - 10;
    var xFor = function(i){ return padL + (n===1?usableW/2:(i/(n-1))*usableW); };
    var yFor = function(t){ return padB + (H-padB-8)*((tmax-t)/(tmax-tmin)); };

    // baseline line
    var svgNS="http://www.w3.org/2000/svg";
    var svg=document.createElementNS(svgNS,"svg");
    svg.setAttribute("width",W); svg.setAttribute("height",H);
    svg.setAttribute("viewBox","0 0 "+W+" "+H);
    svg.style.width="100%"; svg.style.height="150px";

    var base=document.createElementNS(svgNS,"line");
    base.setAttribute("x1",padL); base.setAttribute("y1",H-padB);
    base.setAttribute("x2",W-4); base.setAttribute("y2",H-padB);
    base.setAttribute("stroke","#d7dbe0"); base.setAttribute("stroke-width","1");
    svg.appendChild(base);

    // polyline
    var pts = steps.map(function(s,i){ return xFor(i)+","+yFor(s.temp); }).join(" ");
    var poly=document.createElementNS(svgNS,"polyline");
    poly.setAttribute("points",pts);
    poly.setAttribute("fill","none"); poly.setAttribute("stroke","#2f6fb0"); poly.setAttribute("stroke-width","2.5");
    svg.appendChild(poly);

    steps.forEach(function(s,i){
      var c=document.createElementNS(svgNS,"circle");
      c.setAttribute("cx",xFor(i)); c.setAttribute("cy",yFor(s.temp)); c.setAttribute("r","4");
      c.setAttribute("fill", s.capture?"#b45309":"#2f6fb0");
      svg.appendChild(c);
      var t=document.createElementNS(svgNS,"text");
      t.setAttribute("x",xFor(i)); t.setAttribute("y",yFor(s.temp)-9);
      t.setAttribute("font-size","11"); t.setAttribute("text-anchor","middle"); t.setAttribute("fill","#333");
      t.textContent=(i+1)+" "+s.temp+"\u00b0C";
      svg.appendChild(t);
    });
    c.appendChild(svg);
  }

  // ---------- preview + melt warn ----------
  function updatePreview(){
    document.getElementById("preview").textContent = buildProtocolText();
    var mp = meltPointCount();
    document.getElementById("meltPoints").textContent = mp + " point" + (mp===1?"":"s");
    var totalSteps = steps.length + mp;
    var warn = document.getElementById("meltWarn");
    if (totalSteps > 200){
      warn.textContent = "\u26a0 Total steps ("+totalSteps+") exceed MAX_STEPS=200. Melt ramp will be truncated to "+(200-steps.length)+" points.";
    } else {
      warn.textContent = "";
    }
  }

  // ---------- save / upload ----------
  function downloadProtocol(){
    var text = buildProtocolText();
    var blob = new Blob([text], {type:"text/plain"});
    var a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = "PROTOCOL.TXT";
    document.body.appendChild(a); a.click();
    setTimeout(function(){ URL.revokeObjectURL(a.href); a.remove(); }, 0);
  }

  function uploadToDevice(url){
    var status = document.getElementById("uploadStatus");
    if (!url) { return; }
    url = url.replace(/\/$/,"");
    var text = buildProtocolText();
    var blob = new Blob([text], {type:"text/plain"});
    var fd = new FormData();
    fd.append("protocol", blob, "PROTOCOL.TXT");
    status.textContent = "Uploading...";
    fetch(url + "/upload", { method:"POST", body:fd })
      .then(function(r){ return r.text().then(function(t){ return {ok:r.ok, t:t}; }); })
      .then(function(res){
        status.textContent = res.ok ? ("Uploaded to device ("+res.t+")") : "Upload failed";
      })
      .catch(function(e){ status.textContent = "Upload error: "+e.message; });
  }

  // ---------- init ----------
  document.getElementById("addStep").addEventListener("click", function(){
    steps.push({ name:"New step", temp:72, dur:30, unit:"sec", capture:false });
    renderSteps(); updatePreview();
  });
  document.getElementById("saveBtn").addEventListener("click", function(){
    downloadProtocol();
    var url = document.getElementById("deviceUrl").value.trim();
    if (url) uploadToDevice(url);
  });

  // live-update when melt fields change too
  ["meltFrom","meltTo","meltInc","meltHold"].forEach(function(id){
    document.getElementById(id).addEventListener("input", updatePreview);
  });

  // default date = today
  var d = new Date();
  document.getElementById("date").value = formatDate(d);

  renderSteps();
  renderChart();
  updatePreview();
  window.addEventListener("resize", renderChart);
})();
</script>
<footer style="margin-top:2em;font-size:11px;color:#888;">qPocketPCR-wHRM v0.1 &mdash; forked from qPocketPCR V1.1 (GaudiLabs); &copy; Takakura</footer>
</body>
</html>
)__BUILDER_HTML__";

void handleBuilder() {
  server.send(200, "text/html", BUILDER_HTML);
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>qPocketPCR</title></head><body>";
  html += "<h1>qPocketPCR</h1>";
  html += "<h2>Status</h2>";
  html += "<p>Mode: <span id='mode'>" + getModeString() + "</span></p>";
  html += "<p>Progress: <span id='progress'>" + String(getProgress()) + "</span>%</p>";
  html += "<p>Step: <span id='step'>" + String(PCRstep + 1) + "/" + String(pcrProtocol.stepCount) + "</span></p>";
  html += "<p>Temp: <span id='temp'>" + String(temperature_mean, 1) + "</span> C</p>";
  html += "<h2>Control</h2>";
  html += "<button onclick=\"fetch('/start')\">Start</button> ";
  html += "<button onclick=\"fetch('/stop')\">Stop</button>";
  html += "<h2>Protocol</h2>";
  html += "<p><a href=\"/builder\" target=\"_blank\" style=\"font-weight:bold\">Open Protocol Builder (HRM) &#8594;</a></p>";
  html += "<form action='/upload' method='post' enctype='multipart/form-data'>";
  html += "<input type='file' name='protocol' accept='.txt'><br><br>";
  html += "<button type='submit'>Upload</button></form>";
  html += "<h2>Results</h2>";
  html += "<a href='/download'>Download DATAQPCR.TXT</a>";
  html += "<p style='margin-top:2em;font-size:11px;color:#888;'>qPocketPCR-wHRM v0.1 &mdash; forked from qPocketPCR V1.1 (GaudiLabs); &copy; Takakura</p>";
  html += "</body></html>";
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
    int wifiCaptures = countCaptures();
    if (wifiCaptures >= MAX_MEASUREMENTS) {
      Serial.print("WiFi start: Too many measurements: ");
      Serial.println(wifiCaptures);
      server.send(200, "text/plain", "Too many measurements: " + String(wifiCaptures));
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
  String text = server.arg("protocol");
  if (text.length() == 0) {
    server.send(400, "text/plain", "No protocol data");
    return;
  }
  saveNamedProtocol(name, text);
  // Return the updated list so the UI can refresh.
  String list = listProtocols();
  server.send(200, "text/plain", list);
}
