#include "esp_http_client.h"
#include "esp_camera.h"
#include <WiFi.h>
#include "Arduino.h"
#include <BlynkSimpleEsp32.h>
#include "../src/settings.cpp"

#include "soc/soc.h"          // Disable brownout problems
#include "soc/rtc_cntl_reg.h" // Disable brownout problems

// TODO: napeti baterie https://github.com/espressif/arduino-esp32/issues/102#issuecomment-469711422, https://electronics.stackexchange.com/questions/438418/what-are-the-ai-thinker-esp32-cams-analog-pins
// https://randomnerdtutorials.com/esp32-cam-video-streaming-face-recognition-arduino-ide/
// https://loboris.eu/ESP32/ESP32-CAM%20Product%20Specification.pdf

// https://robotzero.one/time-lapse-esp32-cameras/
// https://robotzero.one/wp-content/uploads/2019/04/Esp32CamTimelapsePost.ino

// TODO: deepSleep interval poslat pres Blynk

Settings settings;

// Deep sleep in seconds
int deep_sleep_interval = 300; 

// Device is connected to WiFi/Blynk and camera is setuped
bool device_connected_and_prepared = false;

// CAMERA_MODEL_AI_THINKER
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

#define analog_pin GPIO_NUM_12

bool init_wifi()
{
  int connAttempts = 0;
  Serial.println("\r\nConnecting to: " + String(settings.wifiSSID));
  WiFi.begin(settings.wifiSSID, settings.wifiPassword);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
    if (connAttempts > 10)
      return false;
    connAttempts++;
  }
  return true;
}

bool init_blynk()
{
  Blynk.config(settings.blynkAuth);
  // timeout 3sec
  Blynk.connect(1000);
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
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  //init with high specs to pre-allocate larger buffers
  if (psramFound())
  {
    // TODO pohrat si s kvalitou, nizsi je lepsi (0 nejlepsi)
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  }
  else
  {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK)
  {
    Serial.printf("Camera init failed with error 0x%x", err);
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

  //TODO: LED blick
  //digitalWrite(GPIO_NUM_4, HIGH);
  //delay(500);

  fb = esp_camera_fb_get();
  if (!fb)
  {
    Serial.println("Camera capture failed");
    return ESP_FAIL;
  }

  //digitalWrite(GPIO_NUM_4, LOW);

  esp_http_client_handle_t http_client;

  esp_http_client_config_t config_client = {0};
  config_client.url = settings.imageUploadScriptUrl;
  config_client.event_handler = _http_event_handler;
  config_client.method = HTTP_METHOD_POST;

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
    Serial.print("HTTP status error: ");
    Serial.println(err);
  }

  esp_http_client_cleanup(http_client);
  esp_camera_fb_return(fb);
  return err;
}

void setupVoltage()
{
  int raw = analogRead(analog_pin);
  float volt = raw / 4095.0f;
  float batteryVoltage = volt * 4.2;
  float batteryLevel = map(raw, 0.0f, 4095.0f, 0, 100);

  Serial.print(" Battery Voltage:   ");
  Serial.print(batteryVoltage);
  Serial.print("   Level:   ");
  Serial.print(batteryLevel);
  Serial.println(" %");

  Blynk.virtualWrite(V3, batteryVoltage);
  Blynk.virtualWrite(V4, batteryLevel);
}

void setup()
{
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector

  Serial.begin(115200);

  // Available ESP32-CAM RTC GPIOs are: 12, 13, 14 and 15
  pinMode(analog_pin, INPUT);

  // camera LED flashlight
  //pinMode(GPIO_NUM_4, OUTPUT);

  if (init_camera())
  {
    Serial.println("Camera OK");
    // delay makes more bright picture (camera has time to boot on)
    delay(10000);

    if (init_wifi())
    {
      Serial.println("Internet connected, connect to Blynk");
      if (init_blynk())
      {
        Serial.println("Blynk connected, setup camera");
        device_connected_and_prepared = true;
      }
      else
      {
        Serial.println("Blynk failed");
      }
    }
    else
    {
      Serial.println("No WiFi");
    }
  }
  else
  {
    Serial.println("Camera init failed.");
  }
}

void loop()
{
  if (device_connected_and_prepared)
  {
    Blynk.virtualWrite(V5, WiFi.localIP().toString());
    Blynk.virtualWrite(V6, WiFi.RSSI());

    setupVoltage();
    take_send_photo();
  }

  // TODO: nastudovat jestli je potreba nebo ne...
  Blynk.disconnect();
  WiFi.disconnect();
  Serial.println("Disconnected WiFi and Blynk done, go to sleep...");
  esp_deep_sleep(deep_sleep_interval * 1000000);
}

// 5.9.2019 21:00 4.11V
// 6.9.2019 21:00 3.94V
