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
String lastRecordedFile = "";  // 添加变量保存最后录制的文件名

// 触摸屏按钮区域定义
#define START_BTN_X 20
#define START_BTN_Y 200
#define STOP_BTN_X 200
#define STOP_BTN_Y 200
#define BTN_WIDTH 100
#define BTN_HEIGHT 50

// SD卡相关常量定义
#define SD_BUFFER_SIZE 512
#define SD_CHECK_INTERVAL 30000  // 30秒检查一次SD卡状态
#define MAX_RETRY_COUNT 3

// SD卡状态变量
bool sdCardReady = false;
unsigned long lastSDCheck = 0;
char sdBuffer[SD_BUFFER_SIZE];
int bufferIndex = 0;

// 显示区域定义
#define STATUS_AREA_HEIGHT 80  // 增加状态栏高度
#define DATA_AREA_Y 80        // 数据区域相应下移
#define DATA_AREA_HEIGHT 110  // 保持数据区域高度不变
#define BUTTON_AREA_Y 200     // 按钮区域相应下移

// 添加新的状态变量
unsigned long lastStatusUpdate = 0;
const unsigned long STATUS_UPDATE_INTERVAL = 1000;  // 状态栏更新间隔（1秒）

// HTML页面模板 - 文件列表页面
const char* fileListTemplate = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>ADS1110 Data Files</title>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial; margin: 20px; }
        .container { max-width: 800px; margin: 0 auto; }
        .card { background: #f0f0f0; padding: 20px; margin: 10px 0; border-radius: 5px; }
        .file-list { list-style: none; padding: 0; }
        .file-item { 
            display: flex; 
            justify-content: space-between; 
            align-items: center;
            padding: 10px;
            border-bottom: 1px solid #ddd;
        }
        .button { 
            background: #4CAF50; 
            color: white; 
            padding: 8px 15px; 
            border: none; 
            border-radius: 5px; 
            cursor: pointer; 
            text-decoration: none;
        }
        .button:hover { background: #45a049; }
        .back-button {
            margin-bottom: 20px;
            display: inline-block;
        }
    </style>
</head>
<body>
    <div class="container">
        <a href="/" class="button back-button">Back to Monitor</a>
        <h1>Recorded Data Files</h1>
        <div class="card">
            <ul class="file-list">
                %s
            </ul>
        </div>
    </div>
</body>
</html>
)rawliteral";

// 修改主页模板，添加查看文件列表的按钮
const char* htmlTemplate = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>ADS1110 Data Monitor</title>
    <meta charset="utf-8">
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
            <h2>Real-time Data</h2>
            <p class="status">Voltage</p>
            <p class="value">%.3f mV</p>
            <p class="status">Timestamp</p>
            <p class="value">%s</p>
            <p class="status">Duration</p>
            <p class="value">%lu seconds</p>
        </div>
        <div class="card">
            <h2>System Status</h2>
            <p>SD Card: %s</p>
            <p>WiFi: %s</p>
            <p>Recording: %s</p>
        </div>
        <div class="card">
            <h2>Operations</h2>
            <a href="/files" class="button">View All Files</a>
            <a href="/download" class="button">Download Current</a>
            <a href="/refresh" class="button">Refresh</a>
        </div>
    </div>
    <script>
        setTimeout(function() {
            window.location.reload();
        }, 5000);
    </script>
</body>
</html>
)rawliteral";

void drawStartButton() {
    M5.Lcd.fillRect(START_BTN_X, BUTTON_AREA_Y, BTN_WIDTH, BTN_HEIGHT, GREEN);
    M5.Lcd.setTextColor(BLACK);
    M5.Lcd.drawCentreString("START", START_BTN_X + BTN_WIDTH / 2, BUTTON_AREA_Y + 15, 2);
}

void drawStopButton() {
    M5.Lcd.fillRect(STOP_BTN_X, BUTTON_AREA_Y, BTN_WIDTH, BTN_HEIGHT, RED);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.drawCentreString("STOP", STOP_BTN_X + BTN_WIDTH / 2, BUTTON_AREA_Y + 15, 2);
}

void clearButtonArea(int x, int y) {
    M5.Lcd.fillRect(x, y, BTN_WIDTH, BTN_HEIGHT, BLACK);
}

// 连接Wi-Fi
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

// NTP时间同步
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

// 修改SD卡错误处理函数
bool checkSDCard() {
    if (!SD.begin()) {
        Serial.println("SD Card initialization failed!");
        sdCardReady = false;
        return false;
    }
    
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("No SD card attached!");
        sdCardReady = false;
        return false;
    }

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Type: %d, Size: %lluMB\n", cardType, cardSize);
    sdCardReady = true;
    return true;
}

// 写入缓冲区数据到SD卡
void flushBuffer() {
    if (bufferIndex > 0 && dataFile) {
        dataFile.write((uint8_t*)sdBuffer, bufferIndex);
        dataFile.flush();
        bufferIndex = 0;
    }
}

// 添加数据到缓冲区
void addToBuffer(const char* data) {
    int dataLen = strlen(data);
    if (bufferIndex + dataLen >= SD_BUFFER_SIZE) {
        flushBuffer();
    }
    strcpy(sdBuffer + bufferIndex, data);
    bufferIndex += dataLen;
}

// 修改文件创建函数
bool createNewDataFile() {
    if (!sdCardReady) {
        Serial.println("Error: SD card not ready");
        return false;
    }

    // 获取当前时间
    M5.Rtc.GetTime(&RTCtime);
    M5.Rtc.GetDate(&RTCdate);
    
    // 生成文件名
    sprintf(fileName, "/ads_%04d%02d%02d_%02d%02d%02d_%03d.csv", 
            RTCdate.Year, RTCdate.Month, RTCdate.Date,
            RTCtime.Hours, RTCtime.Minutes, RTCtime.Seconds,
            millis() % 1000);
    
    Serial.printf("Creating new file: %s\n", fileName);

    // 检查文件是否已存在
    if (SD.exists(fileName)) {
        Serial.println("Warning: File already exists, will append data");
    }

    // 尝试打开文件
    dataFile = SD.open(fileName, FILE_WRITE);
    if (!dataFile) {
        Serial.println("Error: Failed to create file");
        return false;
    }

    // 写入CSV头
    if (dataFile.size() == 0) {
        dataFile.println("Timestamp,Voltage(mV),Duration(s)");
        dataFile.flush();
    }

    Serial.println("File created and opened successfully for recording.");
    lastRecordedFile = String(fileName);
    return true;
}

// 状态显示函数
void updateStatusDisplay() {
    unsigned long currentMillis = millis();
    if (currentMillis - lastStatusUpdate < STATUS_UPDATE_INTERVAL) {
        return;  // 如果距离上次更新时间太短，则不更新
    }
    lastStatusUpdate = currentMillis;
    
    // 保存当前文本颜色
    uint16_t currentTextColor = M5.Lcd.textcolor;
    
    // 清除状态区域
    M5.Lcd.fillRect(0, 0, 320, STATUS_AREA_HEIGHT, BLACK);
    
    // 设置状态文本颜色和大小
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setTextSize(2);  // 使用大字体
    
    // 第一行显示IP地址
    M5.Lcd.setCursor(10, 10);
    M5.Lcd.printf("IP: %s", WiFi.localIP().toString().c_str());
    
    // 第二行显示状态信息
    M5.Lcd.setCursor(10, 40);
    M5.Lcd.printf("SD:%s WiFi:%s REC:%s", 
                  sdCardReady ? "OK" : "ERR",
                  WiFi.status() == WL_CONNECTED ? "OK" : "OFF",
                  recording ? "ON" : "OFF");
    
    // 恢复原始文本颜色
    M5.Lcd.setTextColor(currentTextColor);
}

// 处理根路径请求
void handleRoot() {
    char html[2048];
    snprintf(html, sizeof(html), htmlTemplate,
             lastVoltage,
             lastTimestamp.c_str(),
             lastDuration,
             sdCardReady ? "OK" : "ERROR",
             WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED",
             recording ? "ACTIVE" : "STOPPED"
    );
    server.send(200, "text/html", html);
}

// 修改文件列表处理函数
String getFilesList() {
    String fileList = "";
    if (!sdCardReady) {
        Serial.println("Error: SD card not ready when getting file list");
        return "Error: SD card not ready";
    }

    File root = SD.open("/");
    if (!root) {
        Serial.println("Error: Failed to open root directory");
        return "Error: Failed to open directory";
    }

    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory() && String(file.name()).endsWith(".csv")) {
            String fileName = String(file.name());
            unsigned long fileSize = file.size();
            String fileSizeStr = String(fileSize / 1024.0, 1) + " KB";
            
            fileList += "<li class='file-item'>";
            fileList += "<span>" + fileName + " (" + fileSizeStr + ")</span>";
            fileList += "<a href='/download?file=" + fileName + "' class='button'>Download</a>";
            fileList += "</li>";
        }
        file = root.openNextFile();
    }
    
    if (fileList.length() == 0) {
        fileList = "<li>No data files found</li>";
    }

    root.close();
    return fileList;
}

// 修改数据记录函数
void recordData(float voltage) {
    if (!recording || !sdCardReady || !dataFile) {
        return;
    }

    unsigned long duration = (millis() - startTime) / 1000;
    M5.Rtc.GetTime(&RTCtime);
    M5.Rtc.GetDate(&RTCdate);
    
    // 格式化时间戳（CSV文件使用ISO格式）
    sprintf(str_buffer, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
            RTCdate.Year, RTCdate.Month, RTCdate.Date,
            RTCtime.Hours, RTCtime.Minutes, RTCtime.Seconds,
            millis() % 1000);
    
    // 更新最新数据
    lastVoltage = voltage;
    lastTimestamp = String(str_buffer);
    lastDuration = duration;

    // 写入数据到文件
    String dataString = String(str_buffer) + "," + String(voltage, 6) + "," + String(duration);
    if (dataFile) {
        if (dataFile.println(dataString)) {
            dataFile.flush();
            Serial.println("Timestamp: " + String(str_buffer) + " | Voltage: " + String(voltage, 6) + " mV | Duration: " + String(duration));
        } else {
            Serial.println("Error: Failed to write data to file");
        }
    } else {
        Serial.println("Error: Data file not open");
    }
}

// 处理文件列表请求
void handleFileList() {
    String fileList = getFilesList();
    char html[4096];  // 增加缓冲区大小以容纳更多文件
    snprintf(html, sizeof(html), fileListTemplate, fileList.c_str());
    server.send(200, "text/html", html);
}

// 修改下载处理函数以支持指定文件下载
void handleDownload() {
    String fileName = server.hasArg("file") ? server.arg("file") : lastRecordedFile;
    
    if (fileName.length() == 0) {
        server.send(404, "text/plain", "No file specified");
        return;
    }
    
    if (!fileName.startsWith("/")) {
        fileName = "/" + fileName;
    }
    
    if (!SD.exists(fileName)) {
        server.send(404, "text/plain", "File not found");
        return;
    }
    
    File file = SD.open(fileName, FILE_READ);
    if (!file) {
        server.send(500, "text/plain", "Error opening file");
        return;
    }
    
    String downloadFileName = fileName.startsWith("/") ? fileName.substring(1) : fileName;
    
    server.sendHeader("Content-Type", "text/csv");
    server.sendHeader("Content-Disposition", "attachment; filename=" + downloadFileName);
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
    server.on("/files", handleFileList);  // 添加新的路由
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
    
    // 初始化随机数生成器
    randomSeed(analogRead(0));
    
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

    // SD卡初始化
    sdCardReady = checkSDCard();
    if (!sdCardReady) {
        M5.Lcd.fillScreen(RED);
        M5.Lcd.setTextColor(WHITE);
        M5.Lcd.drawString("SD Card Error!", 10, 10, 2);
        delay(2000);
        M5.Lcd.fillScreen(BLACK);
        return;
    } else {
        M5.Lcd.fillScreen(GREEN);
        M5.Lcd.setTextColor(BLACK);
        M5.Lcd.drawString("SD Card OK!", 10, 10, 2);
        delay(1000);
        M5.Lcd.fillScreen(BLACK);
    }

    // 初始化ADS1100
    ads.getAddr_ADS1100(ADS1100_DEFAULT_ADDRESS);
    ads.setGain(GAIN_ONE);  // 1x 增益(default)
    ads.setMode(MODE_CONTIN);  // 连续转换模式 (default)
    ads.setRate(RATE_8);  // 8SPS (default)
    ads.setOSMode(OSMODE_SINGLE);  // 单次转换模式
    ads.begin();  // 初始化硬件

    // 设置Web服务器
    setupWebServer();

    // 绘制初始界面
    M5.Lcd.fillScreen(BLACK);  // 清屏
    updateStatusDisplay();      // 更新状态栏
    drawStartButton();         // 绘制START按钮
}

void loop(void) {
    M5.update();
    
    // 处理Web请求
    server.handleClient();
    
    unsigned long currentMillis = millis();
    
    // 更新状态显示（现在有1秒的间隔）
    updateStatusDisplay();
    
    // 检查触摸事件
    if (M5.Touch.ispressed()) {
        TouchPoint_t point = M5.Touch.getPressPoint();
        
        // 检查START按钮区域
        if (!recording && point.y >= BUTTON_AREA_Y && point.y <= BUTTON_AREA_Y + BTN_HEIGHT) {
            if (point.x >= START_BTN_X && point.x <= START_BTN_X + BTN_WIDTH) {
                // 尝试打开文件
                if (!createNewDataFile()) {
                    Serial.println("Failed to open file for recording!");
                    M5.Lcd.fillScreen(RED);
                    M5.Lcd.setTextColor(WHITE);
                    M5.Lcd.drawString("File Error!", 10, 10, 2);
                    delay(1000);
                    M5.Lcd.fillScreen(BLACK);
                    updateStatusDisplay();
                    drawStartButton();
                    return;
                }
                
                recording = true;
                startTime = millis();
                clearButtonArea(START_BTN_X, BUTTON_AREA_Y);
                drawStopButton();
                Serial.println("Recording Started");
                delay(100); // 增加防抖动
            }
        }
        // 检查STOP按钮区域
        else if (recording && point.y >= BUTTON_AREA_Y && point.y <= BUTTON_AREA_Y + BTN_HEIGHT) {
            if (point.x >= STOP_BTN_X && point.x <= STOP_BTN_X + BTN_WIDTH) {
                recording = false;
                if (dataFile) {
                    flushBuffer();  // 确保所有数据都已写入
                    dataFile.close();
                    Serial.println("File closed.");
                }
                clearButtonArea(STOP_BTN_X, BUTTON_AREA_Y);
                drawStartButton();
                Serial.println("Recording Stopped");
                delay(100); // 增加防抖动
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

            // 获取当前时间戳（使用更可读的格式）
            M5.Rtc.GetTime(&RTCtime);
            M5.Rtc.GetDate(&RTCdate);
            sprintf(str_buffer, "%02d:%02d:%02d",  // 只显示时分秒
                    RTCtime.Hours, RTCtime.Minutes, RTCtime.Seconds);

            // 更新最新数据
            lastVoltage = voltage;
            lastTimestamp = String(str_buffer);
            lastDuration = duration;

            // 更新LCD显示
            M5.Lcd.fillRect(0, DATA_AREA_Y, 320, DATA_AREA_HEIGHT, BLACK);  // 只清除数据区域
            M5.Lcd.setTextSize(2);  // 设置数据区域文字大小为2
            M5.Lcd.setCursor(10, DATA_AREA_Y + 20);
            M5.Lcd.printf("Voltage: %.3f mV", voltage);
            M5.Lcd.setCursor(10, DATA_AREA_Y + 50);
            M5.Lcd.printf("Time: %s", str_buffer);  // 显示简化后的时间
            M5.Lcd.setCursor(10, DATA_AREA_Y + 80);
            M5.Lcd.printf("Duration: %lu s", duration);

            // 串口输出
            Serial.print("Timestamp: ");
            Serial.print(str_buffer);
            Serial.print(" | Voltage: ");
            Serial.print(voltage, 6);
            Serial.print(" mV | Duration: ");
            Serial.println(duration);

            // 写入数据到SD卡
            recordData(voltage);
            
            // 每100条数据强制刷新一次
            if (bufferIndex > SD_BUFFER_SIZE * 0.8) {
                flushBuffer();
            }
        }
    }
} 