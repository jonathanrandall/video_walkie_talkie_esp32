#ifndef _WIFI_STUFF_
#define _WIFI_STUFF_
#include <WiFi.h>
// #include "espcam_stuff.h"
#include "esp_camera.h"
#include "Arduino.h"
#include "camera_pins.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "camera_index.h"
int init_camera(){
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;//LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;//LEDC_TIMER_0;
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
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000; //20000000
  config.pixel_format =PIXFORMAT_JPEG; ;// PIXFORMAT_RGB565;// PIXFORMAT_JPEG; 
  config.fb_location = CAMERA_FB_IN_PSRAM;//CAMERA_FB_IN_DRAM;//CAMERA_FB_IN_PSRAM;
  config.frame_size = FRAMESIZE_QVGA;
  
  if(psramFound()){
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }
  
  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return -1;
  }

  return 1;
}

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t camera_httpd = NULL;
httpd_handle_t stream_httpd0 = NULL;
httpd_handle_t stream_httpd1 = NULL;
httpd_handle_t stream_httpd2 = NULL;
httpd_handle_t stream_httpd3 = NULL;



typedef struct {
    int image_id;
    char stream_name[32];
} stream_context_t;

// Create an instance of the custom context
stream_context_t my_stream_context0 = {
     0,
    "Camera Stream 0"
};

stream_context_t my_stream_context1 = {
     1,
    "Camera Stream 1"
};

stream_context_t my_stream_context2 = {
     2,
    "Camera Stream 1"
};

stream_context_t my_stream_context3 = {
     3,
    "Camera Stream 1"
};

void sendImage(WiFiClient client, camera_fb_t * fb) {
  if (client.connected()) {
    client.write(fb->buf, fb->len); // Send image data
  }
}

static esp_err_t stream_handler(httpd_req_t *req){
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t * _jpg_buf = NULL;
  char * part_buf[64];

  stream_context_t *ctx = (stream_context_t *)req->user_ctx;

    if (ctx != NULL) {
        Serial.printf("Handling stream for camera: %d, stream name: %s\n", ctx->image_id, ctx->stream_name);
    }
  int ind = ctx->image_id;

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if(res != ESP_OK){
    return res;
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  while(true){
    // xSemaphoreTake(Semaphore_Controls,  portMAX_DELAY);
    
    
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {      
      if(fb->format != PIXFORMAT_JPEG){
        bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
        esp_camera_fb_return(fb);
        fb = NULL;
        if(!jpeg_converted){
          Serial.println("JPEG compression failed");
          res = ESP_FAIL;
        }
      } else {
        // Serial.println("here");
        _jpg_buf_len = fb->len;
        _jpg_buf = fb->buf;
      }      
    }
    if(res == ESP_OK){
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    if(res == ESP_OK){
      size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if(res == ESP_OK){
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }

    
    
    // Serial.println("gave semaphore back");
    
    if(fb){
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if(_jpg_buf){
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    if(res != ESP_OK){
      // xSemaphoreGive(Semaphore_Controls);
      break;
    }
    //Serial.printf("MJPG: %uB\n",(uint32_t)(_jpg_buf_len));
    // esp_camera_fb_return(fb);
  }
  
  
  return res;
}

static esp_err_t index_handler(httpd_req_t *req){
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, (const char *)INDEX_HTML, strlen(INDEX_HTML));
}


void startCameraServer(){
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
//  config.server_port = 80;
  httpd_uri_t index_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = index_handler,
    .user_ctx  = NULL
  };

  httpd_uri_t stream_uri0 = {
    .uri       = "/stream",
    .method    = HTTP_GET,
    .handler   = stream_handler,
    .user_ctx  = &my_stream_context0
  };

  httpd_uri_t stream_uri1 = {
    .uri       = "/stream1",
    .method    = HTTP_GET,
    .handler   = stream_handler,
    .user_ctx  = &my_stream_context1
  };

  httpd_uri_t stream_uri2 = {
    .uri       = "/stream2",
    .method    = HTTP_GET,
    .handler   = stream_handler,
    .user_ctx  = &my_stream_context2
  };

  httpd_uri_t stream_uri3 = {
    .uri       = "/stream3",
    .method    = HTTP_GET,
    .handler   = stream_handler,
    .user_ctx  = &my_stream_context3
  };
  
 
  
  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);

  }
  config.server_port += 1;
  config.ctrl_port += 1;
  if (httpd_start(&stream_httpd0, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd0, &stream_uri0);
    // httpd_register_uri_handler(stream_httpd0, &stream_uri1);
    // httpd_register_uri_handler(stream_httpd0, &stream_uri2);
    // httpd_register_uri_handler(stream_httpd0, &stream_uri3);
  }

  // config.server_port += 1;
  // config.ctrl_port += 1;
  // if (httpd_start(&stream_httpd1, &config) == ESP_OK) {
  //   httpd_register_uri_handler(stream_httpd1, &stream_uri1);
  // }

  // config.server_port += 1;
  // config.ctrl_port += 1;
  // if (httpd_start(&stream_httpd2, &config) == ESP_OK) {
  //   httpd_register_uri_handler(stream_httpd2, &stream_uri2);
  // }

  // config.server_port += 1;
  // config.ctrl_port += 1;
  // if (httpd_start(&stream_httpd3, &config) == ESP_OK) {
  //   httpd_register_uri_handler(stream_httpd3, &stream_uri3);
  // }
}

// void init_wifi()
// {
//   WiFi.mode(WIFI_STA);
//   WiFi.begin(ssid, password);
//   while (WiFi.status() != WL_CONNECTED) {
//     delay(1000);
//     Serial.println("Connecting to WiFi...");
//   }
//     Serial.println("");
//     Serial.println("WiFi connected");
//     Serial.println("IP address: ");
//     Serial.println(WiFi.localIP());
//     Serial.println("");
//     return;

    

    
// }


#endif