#include <Arduino.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h> // Universal Telegram Bot Library written by Brian Lough: https://github.com/witnessmenow/Universal-Arduino-Telegram-Bot
#include <ArduinoJson.h>

#include "camera.h"
#include "config.h"
#include "core0.h"
#include "img_converters.h"



WebSocketsServer ws(82, "", "hub");
TaskHandle_t Task0;
WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);


String sendPhotoTelegram() {
  const char* myDomain = "api.telegram.org";
  String getAll = "";
  String getBody = "";

  //Dispose first picture because of bad quality
  camera_fb_t * fb = NULL;
  esp_camera_fb_return(fb);
  fb = esp_camera_fb_get();
  esp_camera_fb_return(fb); // dispose the buffered image
  
  // Take a new photo
  fb = NULL;  
  fb = esp_camera_fb_get();  
  if(!fb) {
    Serial.println("Camera capture failed");
    delay(1000);
    ESP.restart();
    return "Camera capture failed";
  }  
  
  Serial.println("Connect to " + String(myDomain));


  if (client.connect(myDomain, 443)) {
    Serial.println("Connection successful");
    
    String head = "--Random\r\nContent-Disposition: form-data; name=\"chat_id\"; \r\n\r\n" + String(SUDO) + "\r\n--Random\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"esp32-cam.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
    String tail = "\r\n--Random--\r\n";

    size_t imageLen = fb->len;
    size_t extraLen = head.length() + tail.length();
    size_t totalLen = imageLen + extraLen;
  
    client.println("POST /bot"+String(BOTtoken)+"/sendPhoto HTTP/1.1");
    client.println("Host: " + String(myDomain));
    client.println("Content-Length: " + String(totalLen));
    client.println("Content-Type: multipart/form-data; boundary=Random");
    client.println();
    client.print(head);
  
    uint8_t *fbBuf = fb->buf;
    size_t fbLen = fb->len;
    for (size_t n=0;n<fbLen;n=n+1024) {
      if (n+1024<fbLen) {
        client.write(fbBuf, 1024);
        fbBuf += 1024;
      }
      else if (fbLen%1024>0) {
        size_t remainder = fbLen%1024;
        client.write(fbBuf, remainder);
      }
    }  
    
    client.print(tail);
    
    esp_camera_fb_return(fb);
    
    int waitTime = 10000;   // timeout 10 seconds
    long startTimer = millis();
    boolean state = false;
    
    while ((startTimer + waitTime) > millis()){
      Serial.print(".");
      delay(100);      
      while (client.available()) {
        char c = client.read();
        if (state==true) getBody += String(c);        
        if (c == '\n') {
          if (getAll.length()==0) state=true; 
          getAll = "";
        } 
        else if (c != '\r')
          getAll += String(c);
        startTimer = millis();
      }
      if (getBody.length()>0) break;
    }
    client.stop();
    Serial.println(getBody);
  }
  else {
    getBody="Connected to api.telegram.org failed.";
    Serial.println("Connected to api.telegram.org failed.");
  }
  return getBody;
}




void setup() {
    Serial.begin(115200);
    delay(200);
    cam_init(FRAMESIZE_VGA, PIXFORMAT_JPEG, 10);
    // cam_init(FRAMESIZE_VGA, PIXFORMAT_RGB565);
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.begin(AP_SSID, AP_PASS);
    client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    Serial.println(WiFi.localIP());
    ws.begin();
    xTaskCreatePinnedToCore(core0, "Task0", 10000, NULL, 9, &Task0, 0);
}



void loop() {
    ws.loop();
    
    camera_fb_t *fbj = nullptr;
    fbj = esp_camera_fb_get();
    esp_camera_fb_return(fbj);

    fbj = nullptr;
    fbj = esp_camera_fb_get();
    bufpos_x = xy.getPos(0);
    bufpos_y = xy.getPos(1);

    if (fbj) {
        uint32_t len = fbj->width * fbj->height * 2;
        uint8_t *buf = (uint8_t *)ps_malloc(len);

        if (buf) {
            bool ok = jpg2rgb565(fbj->buf, fbj->len, buf, JPG_SCALE_NONE);
            if (ok) {
                // swap low->high byte
                for (uint32_t i = 0; i < len; i += 2) {
                    uint8_t b = buf[i];
                    buf[i] = buf[i + 1];
                    buf[i + 1] = b;
                }
                face.find(buf, fbj->width, fbj->height);
                if (face.found){
                  Serial.println("Face found");
                  bot.sendMessage(SUDO, "Face found", "");
                  free(buf);
                  esp_camera_fb_return(fbj);
                  sendPhotoTelegram();
                }

                // if (ws.connectedClients()) {
                //     size_t jpg_buf_len = 0;
                //     uint8_t *jpg_buf = nullptr;
                //     ok = fmt2jpg(buf, len, fbj->width, fbj->height, PIXFORMAT_RGB565, 80, &jpg_buf, &jpg_buf_len);
                //     if (ok) ws.broadcastBIN(jpg_buf, jpg_buf_len);
                //     if (jpg_buf) free(jpg_buf);
                // }
            }
            free(buf);
        }
    }
    esp_camera_fb_return(fbj);
    delay(30);
}
