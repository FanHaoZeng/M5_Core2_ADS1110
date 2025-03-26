#include <M5Core2.h>
#include "M5_ADS1100.h"
#include <SPI.h>
#include <SD.h>
#include <TimeLib.h>
#include <TimerEvent.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// Wi-Fi信息
const char* ssid = "GPA7";
const char* password = "yaoyaoaa";

// Web服务器端口
const int webPort = 80;

// NTP客户端设置为布里斯班时区 (UTC+10)
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 10 * 3600, 60000);  // 布里斯班时区

// Web服务器
WebServer server(webPort);

ADS1100 ads;
RTC_TimeTypeDef RTCtime;
RTC_DateTypeDef RTCdate;
char str_buffer[64];
char fileName[25];
File dataFile;
bool recording = false;
unsigned long startTime = 0;
unsigned long previousMillis = 0;
const unsigned long interval = 1000;  // 数据更新间隔（1秒）

// 最新数据
float lastVoltage = 0.0;
String lastTimestamp = "";
unsigned long lastDuration = 0;

// 显示区域定义
#define STATUS_AREA_HEIGHT 40
#define DATA_AREA_Y 50
#define DATA_AREA_HEIGHT 120
#define BUTTON_AREA_Y 180

// 按钮区域定义
#define START_BTN_X 20
#define STOP_BTN_X 200
#define BTN_WIDTH 100
#define BTN_HEIGHT 50

// SD卡状态
bool sdCardReady = false;

// 清除按钮区域函数
void clearButtonArea(int x, int y) {
    M5.Lcd.fillRect(x, y, BTN_WIDTH, BTN_HEIGHT, BLACK);
}

// 连接Wi-Fi函数
void connectWiFi() {
    Serial.print("Connecting to WiFi");
    WiFi.begin(ssid, password);
    int retryCount = 0;
    while (WiFi.status() != WL_CONNECTED && retryCount < 20) {
        delay(500);
        Serial.print(".");
        retryCount++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println(" Connected!");
    } else {
        Serial.println(" Failed to connect!");
    }
}

// NTP时间同步函数
void syncTimeWithNTP() {
    timeClient.begin();
    while (!timeClient.update()) {
        timeClient.forceUpdate();
    }
    unsigned long epochTime = timeClient.getEpochTime();
    struct tm *ptm = gmtime((time_t *)&epochTime);
    RTCdate.Year = ptm->tm_year + 1900;
    RTCdate.Month = ptm->tm_mon + 1;
    RTCdate.Date = ptm->tm_mday;
    RTCtime.Hours = ptm->tm_hour;
    RTCtime.Minutes = ptm->tm_min;
    RTCtime.Seconds = ptm->tm_sec;

    M5.Rtc.SetTime(&RTCtime);
    M5.Rtc.SetDate(&RTCdate);
    Serial.println("Time synchronized with NTP (Brisbane).");
}

// SD卡检查函数
bool checkSDCard() {
    if (!SD.begin()) {
        Serial.println("SD Card initialization failed!");
        return false;
    }
    
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("No SD card attached!");
        return false;
    }
    
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Type: %d, Size: %lluMB\n", cardType, cardSize);
    
    return true;
}

// HTML页面模板
const char* htmlTemplate = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>ADS1110 Data Monitor</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial; margin: 20px; }
        .container { max-width: 800px; margin: 0 auto; }
        .card { background: #f0f0f0; padding: 20px; margin: 10px 0; border-radius: 5px; }
        .status { color: #666; }
        .value { font-size: 24px; font-weight: bold; }
        .button { 
            background: #4CAF50; 
            color: white; 
            padding: 10px 20px; 
            border: none; 
            border-radius: 5px; 
            cursor: pointer; 
            text-decoration: none; 
            display: inline-block; 
            margin: 5px;
        }
        .button:hover { background: #45a049; }
    </style>
</head>
<body>
    <div class="container">
        <h1>ADS1110 Data Monitor</h1>
        <div class="card">
            <h2>实时数据</h2>
            <p class="status">电压值</p>
            <p class="value">%.3f mV</p>
            <p class="status">时间戳</p>
            <p class="value">%s</p>
            <p class="status">持续时间</p>
            <p class="value">%lu 秒</p>
        </div>
        <div class="card">
            <h2>系统状态</h2>
            <p>SD卡状态: %s</p>
            <p>WiFi状态: %s</p>
            <p>录制状态: %s</p>
        </div>
        <div class="card">
            <h2>操作</h2>
            <a href="/download" class="button">下载数据文件</a>
            <a href="/refresh" class="button">刷新数据</a>
        </div>
    </div>
    <script>
        // 自动刷新页面
        setTimeout(function() {
            window.location.reload();
        }, 5000);
    </script>
</body>
</html>
)rawliteral";

// 处理根路径请求
void handleRoot() {
    char html[2048];
    snprintf(html, sizeof(html), htmlTemplate,
             lastVoltage,
             lastTimestamp.c_str(),
             lastDuration,
             sdCardReady ? "正常" : "错误",
             WiFi.status() == WL_CONNECTED ? "已连接" : "未连接",
             recording ? "录制中" : "已停止"
    );
    server.send(200, "text/html", html);
}

// 处理数据下载请求
void handleDownload() {
    if (!SD.exists(fileName)) {
        server.send(404, "text/plain", "File not found");
        return;
    }
    
    File file = SD.open(fileName, FILE_READ);
    if (!file) {
        server.send(500, "text/plain", "Error opening file");
        return;
    }
    
    server.sendHeader("Content-Type", "text/csv");
    server.sendHeader("Content-Disposition", "attachment; filename=data.csv");
    server.streamFile(file, "text/csv");
    file.close();
}

// 处理刷新请求
void handleRefresh() {
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
}

// 处理API请求
void handleAPI() {
    StaticJsonDocument<200> doc;
    doc["voltage"] = lastVoltage;
    doc["timestamp"] = lastTimestamp;
    doc["duration"] = lastDuration;
    doc["recording"] = recording;
    doc["sdCardReady"] = sdCardReady;
    doc["wifiConnected"] = (WiFi.status() == WL_CONNECTED);
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void setupWebServer() {
    server.on("/", handleRoot);
    server.on("/download", handleDownload);
    server.on("/refresh", handleRefresh);
    server.on("/api", handleAPI);
    server.begin();
    Serial.println("Web server started");
    Serial.printf("IP address: %s\n", WiFi.localIP().toString().c_str());
}

void setup(void) {
    M5.begin();
    M5.Lcd.setTextSize(2);
    drawStartButton();

    // 连接Wi-Fi并同步时间
    connectWiFi();
    if (WiFi.status() == WL_CONNECTED) {
        syncTimeWithNTP();
        M5.Lcd.fillScreen(GREEN);
        M5.Lcd.setTextColor(BLACK);
        M5.Lcd.drawString("WiFi Connected!", 10, 10, 2);
        delay(1000);
        M5.Lcd.fillScreen(BLACK);
        
        // 显示IP地址
        M5.Lcd.setTextSize(1);
        M5.Lcd.setCursor(10, 25);
        M5.Lcd.printf("IP: %s", WiFi.localIP().toString().c_str());
        delay(2000);
        M5.Lcd.fillScreen(BLACK);
    } else {
        M5.Lcd.fillScreen(RED);
        M5.Lcd.setTextColor(WHITE);
        M5.Lcd.drawString("WiFi Failed!", 10, 10, 2);
        delay(1000);
        M5.Lcd.fillScreen(BLACK);
    }

    // ... [保留原有的其他初始化代码] ...

    // 设置Web服务器
    setupWebServer();
}

void loop(void) {
    M5.update();
    
    // 处理Web请求
    server.handleClient();
    
    unsigned long currentMillis = millis();
    
    // 检查触摸事件
    if (M5.Touch.ispressed()) {
        TouchPoint_t point = M5.Touch.getPressPoint();
        
        // 检查START按钮区域
        if (!recording && point.y >= BUTTON_AREA_Y && point.y <= BUTTON_AREA_Y + BTN_HEIGHT) {
            if (point.x >= START_BTN_X && point.x <= START_BTN_X + BTN_WIDTH) {
                recording = true;
                startTime = millis();
                clearButtonArea(START_BTN_X, BUTTON_AREA_Y);
                drawStopButton();
                
                // 创建新数据文件
                M5.Rtc.GetTime(&RTCtime);
                M5.Rtc.GetDate(&RTCdate);
                sprintf(fileName, "/ads1110_data_%04d%02d%02d_%02d%02d%02d.csv",
                        RTCdate.Year, RTCdate.Month, RTCdate.Date,
                        RTCtime.Hours, RTCtime.Minutes, RTCtime.Seconds);
                dataFile = SD.open(fileName, FILE_WRITE);
                if (dataFile) {
                    dataFile.println("Timestamp,Voltage(mV),Duration(s)");
                    dataFile.flush();
                }
            }
        }
        // 检查STOP按钮区域
        else if (recording && point.y >= BUTTON_AREA_Y && point.y <= BUTTON_AREA_Y + BTN_HEIGHT) {
            if (point.x >= STOP_BTN_X && point.x <= STOP_BTN_X + BTN_WIDTH) {
                recording = false;
                if (dataFile) {
                    dataFile.close();
                }
                clearButtonArea(STOP_BTN_X, BUTTON_AREA_Y);
                drawStartButton();
            }
        }
    }

    // 仅在录制状态下更新数据
    if (recording && sdCardReady) {
        if (currentMillis - previousMillis >= interval) {
            previousMillis = currentMillis;

            int16_t result = ads.Measure_Differential();
            float voltage = (result / 32768.0) * 2048.0;
            unsigned long duration = (millis() - startTime) / 1000;

            // 获取当前时间戳
            M5.Rtc.GetTime(&RTCtime);
            M5.Rtc.GetDate(&RTCdate);
            sprintf(str_buffer, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                    RTCdate.Year, RTCdate.Month, RTCdate.Date,
                    RTCtime.Hours, RTCtime.Minutes, RTCtime.Seconds, millis() % 1000);

            // 更新最新数据
            lastVoltage = voltage;
            lastTimestamp = String(str_buffer);
            lastDuration = duration;

            // 更新显示
            M5.Lcd.fillRect(0, DATA_AREA_Y, 320, DATA_AREA_HEIGHT, BLACK);
            M5.Lcd.setTextColor(WHITE);
            M5.Lcd.setTextSize(2);
            M5.Lcd.setCursor(10, DATA_AREA_Y + 10);
            M5.Lcd.printf("Voltage: %.3f mV", voltage);
            M5.Lcd.setCursor(10, DATA_AREA_Y + 40);
            M5.Lcd.printf("Time: %02d:%02d:%02d", 
                duration / 3600, (duration % 3600) / 60, duration % 60);

            // 写入数据到SD卡
            sprintf(str_buffer, "%s,%.3f,%lu\n", lastTimestamp.c_str(), voltage, duration);
            if (dataFile) {
                dataFile.print(str_buffer);
                dataFile.flush();
            }
        }
    }
} 