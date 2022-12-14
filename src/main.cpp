#define BLYNK_PRINT Serial
#define BLYNK_TEMPLATE_ID "TMPL2ZKFCUm5"
#define BLYNK_DEVICE_NAME "ESP32 Kamera"
#define BLYNK_FIRMWARE_VERSION "2.0.1"

#include "Arduino.h"
#include "esp_http_client.h"
#include "esp_camera.h"
#include "driver/rtc_io.h"

#include <BlynkSimpleEsp32.h>
#include <TimeLib.h>
#include <EEPROM.h>
#include "driver/adc.h"
#include "../src/settings.cpp"
#include <WidgetRTC.h>
#include <HTTPClient.h>
#include <Update.h>

// Disable brownout problems
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// https://randomnerdtutorials.com/esp32-cam-video-streaming-face-recognition-arduino-ide/
// https://loboris.eu/ESP32/ESP32-CAM%20Product%20Specification.pdf

// https://robotzero.one/time-lapse-esp32-cameras/
// https://robotzero.one/wp-content/uploads/2019/04/Esp32CamTimelapsePost.ino

// is alarm (based on other ESP32 device - Beehive alarm controller)
bool isAlarm = false;

Settings settings;

WiFiClient client;

// Deep sleep interval in seconds
int deep_sleep_interval = 285;

// Deep sleep alarm interval in seconds
int deep_sleep_alarm_interval = 20;

// Use flash
bool use_flash = false;

// Take image depends on current time
bool use_rtc = true;

// maxHour 22:00
int max_hour = 22;
int max_minute = 0;

// min hour 6:00
int min_hour = 6;
int min_minute = 0;

// Device is connected to WiFi/Blynk and camera is setuped
bool device_connected_and_prepared = false;

// CAMERA_MODEL_AI_THINKER
// https://github.com/espressif/arduino-esp32/blob/master/libraries/ESP32/examples/Camera/CameraWebServer/camera_pins.h

#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

// 1 byte in EEPROM
#define EEPROM_SIZE 1
#define WIFI_ATTEMPTS_COUNT 5

// You can use all GPIOs of the header on the button side as analog inputs (12-15,2,4).
// You can define any GPIO pair as your I2C pins, just specify them in the Wire.begin() call.
// BUT analog pins doesn't works after using  WiFi.Begin() !!!

// V0 - deep sleep interval in seconds slider
// V1 - Blynk Image gallery, sended image
// V2 - Terminal
// V3 - deep sleep interval result
// V4 - use flash
// V5 - Local IP
// V6 - WIFi signal
// V7 - version
// V8 - current time
// V9 - setted max/min time
// V10 - time input
// V11 - use rtc
// V12 - is alarm
// V13 - status

// Attach Blynk virtual serial terminal
WidgetTerminal terminal(V2);

// Synchronize settings from Blynk server with device when internet is connected
BLYNK_CONNECTED()
{
  Blynk.sendInternal("rtc", "sync"); // request current local time for device
  Serial.println("Blynk synchronized");
  Blynk.syncAll();
}

// deep sleep interval in seconds
BLYNK_WRITE(V0)
{
  if (param.asInt())
  {
    deep_sleep_interval = param.asInt();
    Serial.println("Deep sleep interval was set to: " + String(deep_sleep_interval));
    Blynk.virtualWrite(V3, String(deep_sleep_interval));
  }
}

String overTheAirURL = "";

BLYNK_WRITE(InternalPinOTA)
{
  overTheAirURL = param.asString();

  Serial.println("OTA Started");
  overTheAirURL = param.asString();
  Serial.print("overTheAirURL = ");
  Serial.println(overTheAirURL);

  HTTPClient http;
  http.begin(overTheAirURL);
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK)
  {
    Serial.println("Bad httpCode");
    return;
  }
  int contentLength = http.getSize();
  if (contentLength <= 0)
  {
    Serial.println("No contentLength");
    return;
  }
  bool canBegin = Update.begin(contentLength);
  if (!canBegin)
  {
    Serial.println("Can't begin update");
    return;
  }
  Client &client = http.getStream();
  int written = Update.writeStream(client);
  if (written != contentLength)
  {
    Serial.println("Bad contentLength");
    return;
  }
  if (!Update.end())
  {
    Serial.println("Update not ended");
    return;
  }
  if (!Update.isFinished())
  {
    Serial.println("Update not finished");
    return;
  }

  Serial.println("Update OK");
  ESP.restart();
}

// set image capture time interval (min and max hour:minute)
BLYNK_WRITE(V10)
{
  TimeInputParam t(param);

  if (t.hasStartTime())
  {
    min_hour = t.getStartHour();
    min_minute = t.getStartMinute();
  }

  if (t.hasStopTime())
  {
    max_hour = t.getStopHour();
    max_minute = t.getStopMinute();
  }
}

// LED flash
BLYNK_WRITE(V4)
{
  if (param.asInt() == 1)
  {
    use_flash = true;
    Serial.println("LED flash was enabled.");
    Blynk.virtualWrite(V4, false);
  }
}

// RTC - use or not use a real time
BLYNK_WRITE(V11)
{
  use_rtc = param.asInt();
}

// Terminal input
BLYNK_WRITE(V2)
{
  String valueFromTerminal = param.asStr();

  if (String("clear") == valueFromTerminal)
  {
    terminal.clear();
    terminal.println("CLEARED");
    terminal.flush();
  }
  else if (String("restart") == valueFromTerminal || String("reset") == valueFromTerminal)
  {
    terminal.clear();
    terminal.println("Restart, bye");
    terminal.flush();
    ESP.restart();
  }
  else if (valueFromTerminal != "\n" || valueFromTerminal != "\r" || valueFromTerminal != "")
  {
    terminal.println(String("unknown command: ") + valueFromTerminal);
    terminal.flush();
  }
}

int get_deep_sleep_interval()
{
  return isAlarm ? deep_sleep_alarm_interval : deep_sleep_interval;
}

bool init_wifi()
{
  int connAttempts = 0;
  Serial.println("\r\nConnecting to: " + String(settings.wifiSSID));
  // try config - quicker for WiFi connection
  // WiFi.config(settings.ip, settings.gateway, settings.subnet, settings.gateway);
  WiFi.begin(settings.wifiSSID, settings.wifiPassword);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
    if (connAttempts > 20)
      return false;
    connAttempts++;
  }
  return true;
}

bool init_blynk()
{
  Blynk.config(settings.blynkAuth);
  // timeout v milisekundach * 3
  Blynk.connect(3000);
  return Blynk.connected();
  // return true;
}

bool init_camera()
{
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
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
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000; // zkusit 16500000 https://github.com/espressif/esp32-camera/issues/150 // 20000000
  config.pixel_format = PIXFORMAT_JPEG;

  // init with high specs to pre-allocate larger buffers
  if (psramFound())
  {
    config.frame_size = FRAMESIZE_UXGA;
    // 0 is best, 63 lowest
    config.jpeg_quality = 20; // zkusit 1
    config.fb_count = 3;      // zkusit 3?
    Serial.printf("Buffer OK");
  }
  else
  {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    Serial.printf("Small buffer!");
  }

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK)
  {
    String problem = "";

    switch (err)
    {
    case ESP_FAIL:
      problem = "Generic esp_err_t code indicating failure";
      break;
    case ESP_ERR_NO_MEM:
      problem = "Out of memory";
      break;
    case ESP_ERR_INVALID_ARG:
      problem = "Invalid argument";
      break;
    case ESP_ERR_INVALID_STATE:
      problem = "Invalid state";
      break;
    case ESP_ERR_INVALID_SIZE:
      problem = "Invalid size";
      break;
    case ESP_ERR_NOT_FOUND:
      problem = "Requested resource not found";
      break;
    case ESP_ERR_NOT_SUPPORTED:
      problem = "Operation or feature not supported";
      break;
    case ESP_ERR_TIMEOUT:
      problem = "Operation timed out";
      break;
    case ESP_ERR_INVALID_RESPONSE:
      problem = "Received response was invalid";
      break;
    case ESP_ERR_INVALID_CRC:
      problem = "CRC or checksum was invalid";
      break;
    case ESP_ERR_INVALID_VERSION:
      problem = "Version was invalid";
      break;
    case ESP_ERR_INVALID_MAC:
      problem = "MAC address was invalid";
      break;
    case ESP_ERR_WIFI_BASE:
      problem = "Starting number of WiFi error codes";
      break;
    case ESP_ERR_MESH_BASE:
      problem = "Starting number of MESH error codes";
      break;
    default:
      problem = String("Unknown error: ") + String(err);
      break;
    }

    Serial.println(problem);

    if (init_wifi())
    {
      if (init_blynk())
      {
        Serial.println("Blynk connected OK, wait to sync");
        Blynk.run();
        Blynk.syncVirtual(V0);
        // delay for Blynk sync
        delay(2000);

        Blynk.virtualWrite(V13, problem);
      }
    }
    return false;
  }
  else
  {
    return true;
  }
}

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
  switch (evt->event_id)
  {
  case HTTP_EVENT_ERROR:
    Serial.println("HTTP_EVENT_ERROR");
    break;
  case HTTP_EVENT_ON_CONNECTED:
    Serial.println("HTTP_EVENT_ON_CONNECTED");
    break;
  case HTTP_EVENT_HEADER_SENT:
    Serial.println("HTTP_EVENT_HEADER_SENT");
    break;
  case HTTP_EVENT_ON_HEADER:
    Serial.println();
    Serial.printf("HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
    break;
  case HTTP_EVENT_ON_DATA:
    Serial.println();
    Serial.printf("HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
    if (!esp_http_client_is_chunked_response(evt->client))
    {
      // Write out data
      // printf("%.*s", evt->data_len, (char*)evt->data);
    }
    break;
  case HTTP_EVENT_ON_FINISH:
    Serial.println("");
    Serial.println("HTTP_EVENT_ON_FINISH");
    break;
  case HTTP_EVENT_DISCONNECTED:
    Serial.println("HTTP_EVENT_DISCONNECTED");
    break;
  }
  return ESP_OK;
}

static esp_err_t take_send_photo()
{
  Serial.println("Taking picture...");
  camera_fb_t *fb = NULL;

  // LED flash blick
  if (use_flash)
  {
    // disable hold (prevents led blinking)
    rtc_gpio_hold_dis(GPIO_NUM_4);
    digitalWrite(GPIO_NUM_4, HIGH);
    delay(500);
  }

  fb = esp_camera_fb_get();
  if (!fb)
  {
    Serial.println("Camera capture failed");
    Blynk.virtualWrite(V13, "Camera capture failed");
    return ESP_FAIL;
  }

  // turn of LED flash
  if (use_flash)
  {
    digitalWrite(GPIO_NUM_4, LOW);
    // enable hold (prevents led blinking)
    rtc_gpio_hold_en(GPIO_NUM_4);
  }

  esp_http_client_handle_t http_client;

  esp_http_client_config_t config_client = {0};
  String url = String(settings.imageUploadScriptUrl) + "?camera=" + String(settings.cameraNumber);
  config_client.url = url.c_str();
  Serial.println("Upload URL: ") + url;
  config_client.event_handler = _http_event_handler;
  config_client.method = HTTP_METHOD_POST;
  config_client.timeout_ms = 5000;

  http_client = esp_http_client_init(&config_client);

  esp_http_client_set_post_field(http_client, (const char *)fb->buf, fb->len);
  esp_http_client_set_header(http_client, "Content-Type", "image/jpg");

  Serial.println("Sending picture to the server...");
  esp_err_t err = esp_http_client_perform(http_client);
  if (err == ESP_OK)
  {
    Serial.print("HTTP status code OK: ");
    Serial.println(esp_http_client_get_status_code(http_client));
  }
  else
  {
    String errorMessage = "HTTP status error: " + String(err);
    Serial.println(errorMessage);
    Blynk.virtualWrite(V13, errorMessage);
  }

  esp_http_client_cleanup(http_client);
  esp_camera_fb_return(fb);
  return err;
}

bool checkLowerTime()
{
  return min_hour < hour() || (min_hour == hour() && min_minute <= minute());
}

bool checkHigherTime()
{
  return max_hour > hour() || (max_hour == hour() && max_minute >= minute());
}

void waitTakeSendPhoto()
{
  // delay makes more bright picture (camera has time to boot on)
  Serial.println("Waiting for taking camera picture.");
  delay(12000);
  take_send_photo();
}

// void checkBeehivesAlarm()
// {
//   isAlarm = false;

//   HTTPClient http;
//   http.begin(client, settings.isAlarm);
//   int httpCode = http.GET();

//   if (httpCode == HTTP_CODE_OK)
//   {
//     String isAlarmValue = http.getString();
//     Serial.println("Alarm: " + isAlarmValue);

//     if (isAlarmValue != "[\"OK\"]")
//     {
//       Serial.println("!!! ALARM, check is alarm enabled !!!");

//       http.begin(client, settings.alarmEnabled);
//       httpCode = http.GET();

//       if (httpCode == HTTP_CODE_OK)
//       {
//         String isAlarmEnabled = http.getString();
//         Serial.println("Beehives alarm enabled: " + isAlarmEnabled);

//         if (isAlarmEnabled == "[\"1\"]")
//         {
//           isAlarm = true;
//           Serial.println("!!! ALARM setted !!!");
//         }
//       }
//       else
//       {
//         Serial.println("Failed verify status of beehives alarm, status code: " + String(httpCode));
//       }
//     }
//     else
//     {
//       Serial.println("No alarm");
//     }
//   }
//   else
//   {
//     Serial.println("Failed verify version beehives alarm, status code: " + String(httpCode));
//   }

//   http.end();
// }

void setup()
{
  // disable brownout detector
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);

  // initialize EEPROM with predefined size
  EEPROM.begin(EEPROM_SIZE);

  if (init_camera())
  {
    Serial.println("Camera OK");

    if (init_wifi())
    {
      Serial.println("Internet connected, connect to Blynk");
      if (init_blynk())
      {
        Serial.print("Blynk connected OK, wait to sync:");
        for (int loop_count = 0; loop_count < 30; loop_count++)
        {
          Blynk.run();
          delay(100);
          Serial.print(".");
        }

        device_connected_and_prepared = true;
        Serial.println("Setup done");

        EEPROM.write(0, 0);
        EEPROM.commit();
      }
      else
      {
        Serial.println("Blynk failed");
      }
    }
    else
    {
      Serial.println("No WiFi");
      int attempsCount = EEPROM.read(0);

      if (attempsCount >= WIFI_ATTEMPTS_COUNT)
      {
        Serial.println("RESTART");
        EEPROM.write(0, 0);
        EEPROM.commit();
        ESP.restart();
      }
      else
      {
        attempsCount += 1;
        Serial.println("Attempts count " + String(attempsCount));
        EEPROM.write(0, attempsCount);
        EEPROM.commit();
      }
    }
  }
  else
  {
    // camera problem - is logged in method..
    Serial.println("camera problem ");
  }
}

void loop()
{
  if (device_connected_and_prepared)
  {
    Serial.println("Set values to Blynk");
    Blynk.virtualWrite(V13, "OK");

    Blynk.virtualWrite(V5, "IP: " + WiFi.localIP().toString() + "|G: " + WiFi.gatewayIP().toString() + "|S: " + WiFi.subnetMask().toString() + "|DNS: " + WiFi.dnsIP().toString());
    Blynk.virtualWrite(V6, WiFi.RSSI());
    Blynk.virtualWrite(V7, settings.version);

    String currentTime = String(hour()) + ":" + minute();
    String minMaxSettedTime = String(min_hour) + ":" + String(min_minute) + " " + String(max_hour) + ":" + String(max_minute);
    Serial.println("Time set: " + minMaxSettedTime);
    Serial.println("Time current: " + currentTime);

    Blynk.virtualWrite(V8, currentTime);
    Blynk.virtualWrite(V9, minMaxSettedTime);

    // checkBeehivesAlarm();
    Blynk.virtualWrite(V12, isAlarm ? "AKTUÁLNÍ ALARM!" : "OK");

    // use flash - take capture always
    if (use_flash)
    {
      // camera LED flashlight
      pinMode(GPIO_NUM_4, OUTPUT);
      Serial.println("Use flash");
      waitTakeSendPhoto();
    }
    // don't user real time - take capture always
    else if (!use_rtc)
    {
      waitTakeSendPhoto();
    }
    // alarm - take photo always
    else if (isAlarm)
    {
      waitTakeSendPhoto();
    }
    // check if time is OK
    else if (checkLowerTime() && checkHigherTime())
    {
      waitTakeSendPhoto();
    }
    // outside of time interval
    else
    {
      Serial.println("Too late for capture image");
    }
  }
  else
  {
    Serial.println("Camera or internet connection is not ready");
  }

  Blynk.disconnect();
  WiFi.disconnect();
  Serial.println("Disconnected WiFi and Blynk done, go to sleep for " + String(get_deep_sleep_interval()) + " seconds.");
  // https://github.com/espressif/arduino-esp32/issues/1113#issuecomment-494132709
  adc_power_release();
  esp_deep_sleep(get_deep_sleep_interval() * 1000000);
}