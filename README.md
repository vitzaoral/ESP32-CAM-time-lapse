# ESP32-CAM time lapse with sending picture to the Cloudinary / Blynk
Time-lapse image capture with ESP32 Camera. Camera makes picture in given interval and sends it to the FTP / CLoudinary server and to the Blynk Image widget.

### Features:
* Sending image in given time laps to the server
* Capture image interval can be set from Blynk
* Taking picture with or without embedded LED flash (can set in the Blynk)
* Can set time interval in Blynk to decide to take or not a image (based on Blynk real time widget and Blynk time input widget)

> To build a project, you need to download all the necessary libraries and create the *settings.cpp* file in the *src* folder:
```c++
#include "IPAddress.h"

// Project settings
struct Settings
{
    const char *blynkAuth = "XXX";
    const int cameraNumber = 1;

    const char *wifiSSID = "YYY";
    const char *wifiPassword = "ZZZ";
    const char *imageUploadScriptUrl = "http://example.com/upload.php";
    const char *version = "1.0.0";
    const char *firmwareVersionUrl = "http://example.com/version.txt";
    const char *firmwareBinUrl = "http://example.com/firmware.bin";

    IPAddress ip = IPAddress(192, 168, 43, 202);
    IPAddress gateway = IPAddress(192, 168, 43, 1);
    IPAddress subnet = IPAddress(255, 255, 255, 0);

    // alarm based on the other ESP32 device
    const char *alarmEnabled = "http://blynk-cloud.com/auth-token/get/V1";
    const char *isAlarm = "http://blynk-cloud.com/auth-token/get/V2";
};
```

### Currents list:

* [ESP32-CAM](https://www.aliexpress.com/item/32992663411.html)

### Sending picture from ESP32-CAM to Cloudinary / Blynk
ESP32-CAM makes picture and send it by HTTP POST request to the server via simple PHP script. I followed [this tutorial](https://robotzero.one/time-lapse-esp32-cameras/).

On the server, PHP script:
* inserts date and time to the picture and saves picture to the disk
* sends picture to the [Cloudinary API](https://cloudinary.com/documentation/upload_images#uploading_with_a_direct_call_to_the_api)
* sets picture public url from Cloudinary to the Blynk Image Widget
* removes picture from server disk

PHP script is located in the *Server scripts folder*. It's needed to set up your *upload_preset*, cloudinary URL, and *blynkAuthToken*. 


### Blynk:
Camera in Blynk iOS application.
![Blynk application](https://github.com/vitzaoral/ESP32-CAM-time-lapse/blob/master/blynk.jpg)

