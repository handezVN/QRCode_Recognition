/*
ESP32-CAM QR code Reader
Author : ChungYi Fu (Kaohsiung, Taiwan)  2021-8-13 20:00
https://www.facebook.com/francefu

Refer to the code
  

自訂指令格式 http://192.168.xxx.xxx/control?cmd=P1;P2;P3;P4;P5;P6;P7;P8;P9

http://192.168.xxx.xxx/?ip                      //取得APIP, STAIP
http://192.168.xxx.xxx/?mac                     //取得MAC位址
http://192.168.xxx.xxx/?digitalwrite=pin;value  //數位輸出
http://192.168.xxx.xxx/?analogwrite=pin;value   //類比輸出
http://192.168.xxx.xxx/?digitalread=pin         //數位讀取
http://192.168.xxx.xxx/?analogread=pin          //類比讀取
http://192.168.xxx.xxx/?touchread=pin           //觸碰讀取
http://192.168.xxx.xxx/?restart                 //重啟電源
http://192.168.xxx.xxx/?flash=value             //閃光燈 value= 0~255
http://192.168.xxx.xxx/?servo=pin;value         //伺服馬達 value= 0~180
http://192.168.xxx.xxx/?relay=pin;value         //繼電器 value = 0, 1
http://192.168.xxx.xxx/?uart=value              //序列埠
*/
#include <HTTPClient.h>
//輸入WIFI連線帳號密碼
const char* ssid = "Sunny1-2.4G";
const char* password = "sunny123";

//輸入AP端連線帳號密碼  http://192.168.4.1
const char* apssid = "esp32-cam";
const char* appassword = "12345678";         //AP密碼至少要8個字元以上  

String Feedback="";   //回傳客戶端訊息
//指令參數值
String Command="",cmd="",P1="",P2="",P3="",P4="",P5="",P6="",P7="",P8="",P9="";
//指令拆解狀態值
byte ReceiveState=0,cmdState=1,strState=1,questionstate=0,equalstate=0,semicolonstate=0;

#include <WiFi.h>
#include "esp_camera.h"          //chức năng video
#include "soc/soc.h"             //Sử dụng cho nguồn điện không ổn định
#include "soc/rtc_cntl_reg.h"    //Sử dụng cho nguồn điện không ổn định
#include "quirc.h"

TaskHandle_t Task;

//ESP32-CAM 
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

struct QRCodeData
{
  bool valid;
  int dataType;
  uint8_t payload[1024];
  int payloadLen;
};

struct quirc *q = NULL;
uint8_t *image = NULL;  
camera_fb_t * fb = NULL;
struct quirc_code code;
struct quirc_data data;
quirc_decode_error_t err;
struct QRCodeData qrCodeData;  
String QRCodeResult = "";
String keyword = "**************" ;
WiFiServer server(80);
WiFiClient client;

camera_config_t config;

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);  //Cài đặt khởi động lại khi tắt nguồn không ổn định
    
  Serial.begin(115200);
  Serial.setDebugOutput(true);  //Bật đầu ra chẩn đoán
  Serial.println();
  
//Cài đặt cấu hình video  https://github.com/espressif/esp32-camera/blob/master/driver/include/esp_camera.h
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
  config.xclk_freq_hz = 10000000;
  config.pixel_format = PIXFORMAT_GRAYSCALE;
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 15;
  config.fb_count = 1;

  //Khởi tạo video
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    ESP.restart();
  }

  sensor_t * s = esp_camera_sensor_get();
  s->set_framesize(s, FRAMESIZE_QVGA);

  //s->set_vflip(s, 1);  //Dọc
  //s->set_hmirror(s, 1);  //Ngang
          
  //Flash(GPIO4)
  ledcAttachPin(4, 4);  
  ledcSetup(4, 5000, 8); 
  
  WiFi.mode(WIFI_AP_STA);  //các chế độ khác WiFi.mode(WIFI_AP); WiFi.mode(WIFI_STA);

 
  for (int i=0;i<2;i++) {
    WiFi.begin(ssid, password);    
  
    delay(1000);
    Serial.println("");
    Serial.print("Connecting to ");
    Serial.println(ssid);
    
    long int StartTime=millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        if ((StartTime+5000) < millis()) break;    //Chờ 10s để kết nối
    } 
  
    if (WiFi.status() == WL_CONNECTED) {    //Nếu kết nối thành công
      Serial.println("");
      Serial.println("ESP IP Address: http://");
      Serial.println(WiFi.localIP());
      Serial.println("");
      HTTPClient http;
      http.begin("http://98c4-115-73-223-44.ngrok.io/"); //Specify the URL
      int httpCode = http.GET();                        //Make the request
  
      if (httpCode > 0) { //Check for the returning code
  
        String payload = http.getString();
        if(payload!= "" ) {
          keyword = payload ;
        }
        Serial.println(httpCode);
        Serial.println(payload);
      }
      else {
        Serial.println("Error on HTTP request");
      }

    http.end(); //Free the resources
      for (int i=0;i<5;i++) {   //Nếu được kết nối với WIFI, hãy đặt đèn flash nhấp nháy nhanh
        ledcWrite(4,10);
        delay(200);
        ledcWrite(4,0);
        delay(200);    
      }
      break;
    }
  } 

  if (WiFi.status() != WL_CONNECTED) {    //Nếu kết nối không thành công
    Serial.println("Wifi connected : Failed");
    for (int i=0;i<2;i++) {    //Nếu bạn không thể kết nối với WIFI, hãy đặt đèn flash nhấp nháy chậm
      ledcWrite(4,10);
      delay(1000);
      ledcWrite(4,0);
      delay(1000);    
    }
  } 
  
  //指定AP端IP
  //WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0)); 
  Serial.println("");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());  
  Serial.println("");
  
  //設定閃光燈為低電位
  pinMode(4, OUTPUT);
  digitalWrite(4, LOW); 

  server.begin();  

  xTaskCreatePinnedToCore(
             QRCodeReader, /* Task function. */
             "Task",   /* name of task. */
             10000,     /* Stack size of task */
             NULL,      /* parameter of the task */
             1,         /* priority of the task */
             &Task,    /* Task handle to keep track of created task */
             0);        /* pin task to core 0 */

  Serial.print("listenConnection running on core ");
  Serial.println(xPortGetCoreID());   
}

void loop() {
  listenConnection();  
}

void QRCodeReader( void * pvParameters ){
  Serial.print("QRCodeReader running on core ");
  Serial.println(xPortGetCoreID());

  while(1){
      q = quirc_new();
      if (q == NULL){
        Serial.print("can't create quirc object\r\n");  
        continue;
      }
    
      fb = esp_camera_fb_get();
      if (!fb)
      {
        Serial.println("Camera capture failed");
        continue;
      }   
      
      //Serial.printf("quirc_begin\r\n");
      quirc_resize(q, fb->width, fb->height);
      image = quirc_begin(q, NULL, NULL);
      //Serial.printf("Frame w h len: %d, %d, %d \r\n", fb->width, fb->height, fb->len);
      memcpy(image, fb->buf, fb->len);
      quirc_end(q);
      //Serial.printf("quirc_end\r\n");
      
      int count = quirc_count(q);
      if (count > 0) {
        Serial.println(count);
        quirc_extract(q, 0, &code);
        err = quirc_decode(&code, &data);
    
        if (err){
          Serial.println("Decoding FAILED");
          QRCodeResult = "Decoding FAILED";
        } else {
          Serial.printf("Decoding successful:\n");
          dumpData(&data);
        } 
        Serial.println();
      } 
      
      esp_camera_fb_return(fb);
      fb = NULL;
      image = NULL;  
      quirc_destroy(q);
  }
}

void dumpData(const struct quirc_data *data)
{
  Serial.printf("Version: %d\n", data->version);
  Serial.printf("ECC level: %c\n", "MLHQ"[data->ecc_level]);
  Serial.printf("Mask: %d\n", data->mask);
  Serial.printf("Length: %d\n", data->payload_len);
  Serial.printf("Payload: %s\n", data->payload);
  Serial.printf("Keyword: %s\n", keyword);
  String tmp = String((char *)data->payload);
  if(tmp == keyword){
    Serial.printf("Door: Open");
    for (int i=0;i<1;i++) {    
      ledcWrite(4,10);
      delay(3000);
      ledcWrite(4,0);
      delay(1000);    
    }
  }else{
    Serial.printf("Door: Locked");
  };
  QRCodeResult = (const char *)data->payload;
}

//執行自訂指令
void ExecuteCommand() {
  //Serial.println("");
  //Serial.println("Command: "+Command);
  if (cmd!="getstill") {
    Serial.println("cmd= "+cmd+" ,P1= "+P1+" ,P2= "+P2+" ,P3= "+P3+" ,P4= "+P4+" ,P5= "+P5+" ,P6= "+P6+" ,P7= "+P7+" ,P8= "+P8+" ,P9= "+P9);
    Serial.println("");
  }

  //自訂指令區塊  http://192.168.xxx.xxx?cmd=P1;P2;P3;P4;P5;P6;P7;P8;P9
  if (cmd=="your cmd") {
    // You can do anything
    // Feedback="<font color=\"red\">Hello World</font>";   //可為一般文字或HTML語法
  }
  else if (cmd=="ip") {  //查詢APIP, STAIP
    Feedback="AP IP: "+WiFi.softAPIP().toString();    
    Feedback+="<br>";
    Feedback+="STA IP: "+WiFi.localIP().toString();
  }  
  else if (cmd=="mac") {  //查詢MAC位址
    Feedback="STA MAC: "+WiFi.macAddress();
  }  
  else if (cmd=="restart") {  //重設WIFI連線
    ESP.restart();
  }  
  else if (cmd=="digitalwrite") {  //數位輸出
    ledcDetachPin(P1.toInt());
    pinMode(P1.toInt(), OUTPUT);
    digitalWrite(P1.toInt(), P2.toInt());
  }   
  else if (cmd=="digitalread") {  //數位輸入
    Feedback=String(digitalRead(P1.toInt()));
  }
  else if (cmd=="analogwrite") {  //類比輸出
    if (P1=="4") {
      ledcAttachPin(4, 4);  
      ledcSetup(4, 5000, 8);
      ledcWrite(4,P2.toInt());     
    }
    else {
      ledcAttachPin(P1.toInt(), 9);
      ledcSetup(9, 5000, 8);
      ledcWrite(9,P2.toInt());
    }
  }       
  else if (cmd=="analogread") {  //類比讀取
    Feedback=String(analogRead(P1.toInt()));
  }
  else if (cmd=="touchread") {  //觸碰讀取
    Feedback=String(touchRead(P1.toInt()));
  }  
  else if (cmd=="framesize") {  //解析度
    sensor_t * s = esp_camera_sensor_get();
    int val = P1.toInt();
    s->set_framesize(s, (framesize_t)val);   
  }
  else if (cmd=="quality") {  //畫質 
    sensor_t * s = esp_camera_sensor_get();
    int val = P1.toInt(); 
    s->set_quality(s, val);
  }
  else if (cmd=="contrast") {  //對比
    sensor_t * s = esp_camera_sensor_get();
    int val = P1.toInt(); 
    s->set_contrast(s, val);
  }
  else if (cmd=="brightness") {  //亮度
    sensor_t * s = esp_camera_sensor_get();
    int val = P1.toInt();  
    s->set_brightness(s, val);  
  } 
  else if (cmd=="hmirror") {  //水平鏡像
    sensor_t * s = esp_camera_sensor_get();
    int val = P1.toInt();  
    s->set_hmirror(s, val); 
  }   
  else if (cmd=="vflip") {  //垂直翻轉
    sensor_t * s = esp_camera_sensor_get();
    int val = P1.toInt();  
    s->set_vflip(s, val);
  }
  else if (cmd=="serial") {  //序列埠
    Serial.print(P1);
  }  
  else if (cmd=="restart") {  //重啟電源
    ESP.restart();
  }         
  else if (cmd=="flash") {  //閃光燈
    ledcAttachPin(4, 4);  
    ledcSetup(4, 5000, 8);   
    int val = P1.toInt();
    ledcWrite(4,val);  
  }
  else if(cmd=="servo") {  //伺服馬達 (0-180)
    ledcAttachPin(P1.toInt(), 3);
    ledcSetup(3, 50, 16);
     
    int val = 7864-P2.toInt()*34.59; 
    if (val > 7864)
       val = 7864;
    else if (val < 1638)
      val = 1638; 
    ledcWrite(3, val);
  }
  else if (cmd=="relay") {  //繼電器
    pinMode(P1.toInt(), OUTPUT);  
    digitalWrite(13, P2.toInt());  
  }       
  else {
    Feedback="Command is not defined.";
  }  
  
  if (Feedback=="") Feedback=Command;  
}


//自訂網頁首頁管理介面
static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<head>
  <title></title>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
</head>
<body>
<canvas id="canvas" width="320" height="240"></canvas><br>
Flash<input type="range" id="flash" min="0" max="255" value="0">
<input type="button" value="Get Still" onclick="getStill();"><br>
<div id="result" style="color:red"></div>
</body>
</html> 
  
<script>
  var canvas = document.getElementById('canvas'); 
  var context = canvas.getContext('2d');
  var flash = document.getElementById('flash');
  var result = document.getElementById('result');
  
  flash.onchange = function() {
    var query = document.location.origin+"/?flash="+flash.value;
    fetch(query);
  }

  function getStill() {
    var xhr = new XMLHttpRequest();
    xhr.open("GET", "/?getstill", true);
    xhr.responseType = "arraybuffer";
    
    xhr.onload = function (oEvent) {
      var arrayBuffer = xhr.response; // Note: not xhr.responseText
      if (arrayBuffer) {
        var byteArray = new Uint8Array(arrayBuffer);
        var imgData=context.getImageData(0,0,canvas.width,canvas.height);
        var val = 0;
        for (var i=0;i<imgData.data.length;i+=4) {
          val = parseInt(byteArray[i/4], 10);
              imgData.data[i]=val;
              imgData.data[i+1]=val;
              imgData.data[i+2]=val;
              imgData.data[i+3]=255;
          }
          context.putImageData(imgData,0,0);
          setTimeout(function(){getStill();}, 100);
        }
    };
    
    xhr.send(null);
  }
</script>   
)rawliteral";

void listenConnection() {
  Feedback="";Command="";cmd="";P1="";P2="";P3="";P4="";P5="";P6="";P7="";P8="";P9="";
  ReceiveState=0,cmdState=1,strState=1,questionstate=0,equalstate=0,semicolonstate=0;
  
  client = server.available();
  
  if (client) { 
    String currentLine = "";

    while (client.connected()) {
      if (client.available()) {
        char c = client.read(); 
        getCommand(c);   //將緩衝區取得的字元拆解出指令參數
        if (c == '\n') {
          if (currentLine.length() == 0) {
            if (cmd=="getstill") {
              getStill();            
            } else {
              mainPage();
            }         
            Feedback="";
            break;
          } else {
            currentLine = "";
          }
        } else if (c != '\r') {
          currentLine += c;
        }

        if ((currentLine.indexOf("?")!=-1)&&(currentLine.indexOf(" HTTP")!=-1)) {
          if (Command.indexOf("stop")!=-1) {  //若指令中含關鍵字stop立即斷線 -> http://192.168.xxx.xxx?cmd=aaa;bbb;ccc;stop
            client.println();
            client.println();
            client.stop();
          }
          currentLine="";
          Feedback="";
          ExecuteCommand();
        }
      }
    }
    delay(1);
    client.stop();
  }
}

void mainPage() {
    //回傳HTML首頁或Feedback
    client.println("HTTP/1.1 200 OK");
    client.println("Access-Control-Allow-Headers: Origin, X-Requested-With, Content-Type, Accept");
    client.println("Access-Control-Allow-Methods: GET,POST,PUT,DELETE,OPTIONS");
    client.println("Content-Type: text/html; charset=utf-8");
    client.println("Access-Control-Allow-Origin: *");
    client.println("Connection: close");
    client.println();
    
    String Data="";
    if (cmd!="")
      Data = Feedback;
    else {
      Data = String((const char *)INDEX_HTML);
    }
    int Index;
    for (Index = 0; Index < Data.length(); Index = Index+1024) {
      client.print(Data.substring(Index, Index+1024));
    }
}

void getStill() {
  camera_fb_t * fb = NULL;
  fb = esp_camera_fb_get();  
  if(!fb) {
    Serial.println("Camera capture failed");
    return;
  }
  uint8_t *fbBuf = fb->buf;
  size_t fbLen = fb->len; 
  
  client.println("HTTP/1.1 200 OK");
  client.println("Access-Control-Allow-Origin: *");              
  client.println("Access-Control-Allow-Headers: Origin, X-Requested-With, Content-Type, Accept");
  client.println("Access-Control-Allow-Methods: GET,POST,PUT,DELETE,OPTIONS");
  client.println("Content-Type: application/octet-stream");
  client.println();

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
  esp_camera_fb_return(fb);

  pinMode(4, OUTPUT);
  digitalWrite(4, LOW);
}

//拆解命令字串置入變數
void getCommand(char c) {
  if (c=='?') ReceiveState=1;
  if ((c==' ')||(c=='\r')||(c=='\n')) ReceiveState=0;
  
  if (ReceiveState==1)
  {
    Command=Command+String(c);
    
    if (c=='=') cmdState=0;
    if (c==';') strState++;
  
    if ((cmdState==1)&&((c!='?')||(questionstate==1))) cmd=cmd+String(c);
    if ((cmdState==0)&&(strState==1)&&((c!='=')||(equalstate==1))) P1=P1+String(c);
    if ((cmdState==0)&&(strState==2)&&(c!=';')) P2=P2+String(c);
    if ((cmdState==0)&&(strState==3)&&(c!=';')) P3=P3+String(c);
    if ((cmdState==0)&&(strState==4)&&(c!=';')) P4=P4+String(c);
    if ((cmdState==0)&&(strState==5)&&(c!=';')) P5=P5+String(c);
    if ((cmdState==0)&&(strState==6)&&(c!=';')) P6=P6+String(c);
    if ((cmdState==0)&&(strState==7)&&(c!=';')) P7=P7+String(c);
    if ((cmdState==0)&&(strState==8)&&(c!=';')) P8=P8+String(c);
    if ((cmdState==0)&&(strState>=9)&&((c!=';')||(semicolonstate==1))) P9=P9+String(c);
    
    if (c=='?') questionstate=1;
    if (c=='=') equalstate=1;
    if ((strState>=9)&&(c==';')) semicolonstate=1;
  }
}
