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
#include <WebSocketsServer.h>
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
WebSocketsServer ws(81);  // WebSocket服务器在81端口

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

// 修改handleRoot函数
void handleRoot() {
    // 添加CORS头
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    
    String html = "<!DOCTYPE html><html><head>";
    html += "<title>ADS1110 Data Logger</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body { font-family: Arial; margin: 20px; }";
    html += ".container { max-width: 800px; margin: 0 auto; }";
    html += ".status { margin: 20px 0; padding: 10px; border-radius: 5px; }";
    html += ".recording { background-color: #d4edda; color: #155724; }";
    html += ".not-recording { background-color: #f8d7da; color: #721c24; }";
    html += "</style>";
    html += "</head><body>";
    html += "<div class='container'>";
    html += "<h1>ADS1110 Data Logger Status</h1>";
    html += "<div class='status'>";
    html += "<p>Device IP: " + WiFi.localIP().toString() + "</p>";
    html += "<p>WebSocket Port: 81</p>";
    html += "<p>Status: " + String(recording ? "Recording" : "Not Recording") + "</p>";
    html += "</div>";
    html += "<p>Please run the data visualization program on your computer to view the real-time data.</p>";
    html += "</div></body></html>";
    
    server.send(200, "text/html", html);
}

// 修改handleFileList函数
void handleFileList() {
    // 添加CORS头
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    
    String html = "<!DOCTYPE html><html><head>";
    html += "<title>Recorded Files</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body { font-family: Arial; margin: 20px; }";
    html += ".container { max-width: 800px; margin: 0 auto; }";
    html += ".file-list { margin: 20px 0; }";
    html += ".file-item { padding: 15px; margin: 10px 0; background-color: #f8f9fa; border-radius: 5px; display: flex; justify-content: space-between; align-items: center; }";
    html += ".button { display: inline-block; padding: 10px 20px; margin: 5px; border-radius: 5px; text-decoration: none; color: white; transition: all 0.3s; }";
    html += ".download { background-color: #28a745; }";
    html += ".download:hover { background-color: #218838; }";
    html += ".back { background-color: #6c757d; }";
    html += ".back:hover { background-color: #5a6268; }";
    html += ".file-name { font-size: 16px; }";
    html += ".file-size { color: #666; margin-left: 10px; }";
    html += "</style>";
    html += "</head><body>";
    html += "<div class='container'>";
    html += "<h1>Recorded Files</h1>";
    html += "<a href='/' class='button back'>Back to Monitor</a>";
    
    File root = SD.open("/");
    if (!root) {
        html += "<p>Failed to open directory</p>";
    } else {
        File file = root.openNextFile();
        while (file) {
            if (!file.isDirectory() && String(file.name()).endsWith(".csv")) {
                html += "<div class='file-item'>";
                html += "<div class='file-info'>";
                html += "<span class='file-name'>" + String(file.name()) + "</span>";
                html += "<span class='file-size'>(" + String(file.size()) + " bytes)</span>";
                html += "</div>";
                html += "<a href='/download?file=" + String(file.name()) + "' class='button download'>Download</a>";
                html += "</div>";
            }
            file = root.openNextFile();
        }
        root.close();
    }
    
    html += "</div></body></html>";
    
    server.send(200, "text/html", html);
}

void handleRefresh() {
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleAPI() {
    // 添加CORS头
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    
    StaticJsonDocument<200> doc;
    doc["voltage"] = lastVoltage;
    doc["timestamp"] = lastTimestamp;
    doc["duration"] = lastDuration;
    doc["recording"] = recording;
    doc["sdcard"] = sdCardReady;
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleStart() {
    // 添加CORS头
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    
    if (!recording) {
        if (createNewDataFile()) {
            recording = true;
            startTime = millis();
            // 更新屏幕按钮
            clearButtonArea(START_BTN_X, BUTTON_AREA_Y);
            drawStopButton();
        }
    }
    server.send(200, "text/plain", "Recording started");
}

void handleStop() {
    // 添加CORS头
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    
    if (recording) {
        recording = false;
        if (dataFile) {
            flushBuffer();  // 确保所有数据都已写入
            dataFile.close();
        }
        // 更新屏幕按钮
        clearButtonArea(STOP_BTN_X, BUTTON_AREA_Y);
        drawStartButton();
    }
    server.send(200, "text/plain", "Recording stopped");
}

// 修改WebSocket事件处理函数
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.printf("[WebSocket] Client #%u Disconnected\n", num);
            break;
        case WStype_CONNECTED:
            {
                IPAddress ip = ws.remoteIP(num);
                Serial.printf("[WebSocket] Client #%u Connected from %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
                
                // 发送当前状态
                StaticJsonDocument<200> doc;
                doc["voltage"] = lastVoltage;
                doc["timestamp"] = lastTimestamp;
                doc["duration"] = lastDuration;
                String jsonString;
                serializeJson(doc, jsonString);
                ws.sendTXT(num, jsonString);
            }
            break;
        case WStype_TEXT:
            Serial.printf("[WebSocket] Received text: %s\n", payload);
            break;
        case WStype_ERROR:
            Serial.printf("[WebSocket] Error occurred for client #%u\n", num);
            break;
        case WStype_PING:
            Serial.printf("[WebSocket] Received ping from client #%u\n", num);
            break;
        case WStype_PONG:
            Serial.printf("[WebSocket] Received pong from client #%u\n", num);
            break;
    }
}

// 修改setupWebServer函数
void setupWebServer() {
    // 设置WebSocket事件处理程序
    ws.begin();
    ws.onEvent(webSocketEvent);

    // 设置路由处理程序
    server.on("/", HTTP_GET, handleRoot);
    server.on("/files", HTTP_GET, handleFileList);
    server.on("/download", HTTP_GET, handleDownload);
    server.on("/refresh", HTTP_GET, handleRefresh);
    server.on("/api", HTTP_GET, handleAPI);
    server.on("/start", HTTP_GET, handleStart);
    server.on("/stop", HTTP_GET, handleStop);
    
    // 启动服务器
    server.begin();
    Serial.println("HTTP server started");
    Serial.printf("WebSocket server started at ws://%s:81\n", WiFi.localIP().toString().c_str());
}

// 添加handleDownload函数
void handleDownload() {
    // 添加CORS头
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    
    String fileName = server.hasArg("file") ? server.arg("file") : lastRecordedFile;
    if (!fileName.startsWith("/")) {
        fileName = "/" + fileName;
    }
    
    if (!SD.exists(fileName)) {
        server.send(404, "text/plain", "File not found");
        return;
    }
    
    File file = SD.open(fileName, "r");
    if (!file) {
        server.send(500, "text/plain", "Error opening file");
        return;
    }
    
    server.sendHeader("Content-Type", "text/csv");
    server.sendHeader("Content-Disposition", "attachment; filename=" + fileName.substring(1));
    server.streamFile(file, "text/csv");
    file.close();
}

// 添加recordData函数
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
            Serial.println("Data written: " + dataString);
        } else {
            Serial.println("Error: Failed to write data to file");
        }
    } else {
        Serial.println("Error: Data file not open");
    }
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
    ws.loop();
    
    unsigned long currentMillis = millis();
    
    // 更新状态显示
    if (currentMillis - lastStatusUpdate >= STATUS_UPDATE_INTERVAL) {
        updateStatusDisplay();
        lastStatusUpdate = currentMillis;
    }
    
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

    // 在发送WebSocket数据时添加错误检查
    if (recording && sdCardReady) {
        if (currentMillis - previousMillis >= interval) {
            previousMillis = currentMillis;

            int16_t result = ads.Measure_Differential();
            // float voltage = ((result / 2047.0) * 3.3) * 1000.0;  // 将结果转换为毫伏
            
            // 转换为 ADS1100 模拟电压（V）
            float voltage_ads = (float)result / 32768.0 * 2.048;

            // ADC Unit 内部分压比例（12V 映射到 2.048V）
            float scale = 12.0 / 2.048;

            // 反推输入电压
            float voltage = voltage_ads * scale * 1000;

            unsigned long duration = (millis() - startTime) / 1000;

            // 获取当前时间戳
            M5.Rtc.GetTime(&RTCtime);
            M5.Rtc.GetDate(&RTCdate);
            sprintf(str_buffer, "%02d:%02d:%02d",
                    RTCtime.Hours, RTCtime.Minutes, RTCtime.Seconds);

            // 创建JSON数据
            StaticJsonDocument<200> doc;
            doc["voltage"] = voltage;
            doc["timestamp"] = str_buffer;
            doc["duration"] = duration;
            
            String jsonString;
            serializeJson(doc, jsonString);
            
            // 发送WebSocket数据并检查错误
            if (ws.connectedClients() > 0) {
                ws.broadcastTXT(jsonString);
                Serial.println("[WebSocket] Data sent: " + jsonString);
            }

            // 更新LCD显示
            M5.Lcd.fillRect(0, DATA_AREA_Y, 320, DATA_AREA_HEIGHT, BLACK);
            M5.Lcd.setTextSize(2);
            M5.Lcd.setCursor(10, DATA_AREA_Y + 20);
            M5.Lcd.printf("Voltage: %.3f mV", voltage);
            M5.Lcd.setCursor(10, DATA_AREA_Y + 50);
            M5.Lcd.printf("Time: %s", str_buffer);
            M5.Lcd.setCursor(10, DATA_AREA_Y + 80);
            M5.Lcd.printf("Duration: %lu s", duration);

            // 写入数据到SD卡
            recordData(voltage);
            
            if (bufferIndex > SD_BUFFER_SIZE * 0.8) {
                flushBuffer();
            }
        }
    }
} 