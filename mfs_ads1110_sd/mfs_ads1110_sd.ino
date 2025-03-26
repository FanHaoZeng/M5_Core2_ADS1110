#include <M5Core2.h>
#include "M5_ADS1100.h"
#include <SPI.h>
#include <SD.h>
#include <TimeLib.h>
#include <TimerEvent.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>

// Wi-Fi信息
const char* ssid = "GPA7";
const char* password = "yaoyaoaa";


// NTP客户端设置为布里斯班时区 (UTC+10)
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 10 * 3600, 60000);  // 布里斯班时区

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
#define STATUS_AREA_HEIGHT 60  // 保持状态栏高度
#define DATA_AREA_Y 60        // 减小间隙，直接从状态栏下方开始
#define DATA_AREA_HEIGHT 110  // 稍微增加数据区域高度
#define BUTTON_AREA_Y 180

// 添加新的状态变量
unsigned long lastStatusUpdate = 0;
const unsigned long STATUS_UPDATE_INTERVAL = 1000;  // 状态栏更新间隔（1秒）

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

// SD卡错误处理函数
bool checkSDCard() {
    if (!SD.begin()) {
        Serial.println("SD Card initialization failed!");
        return false;
    }
    
    // 检查SD卡类型和容量
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("No SD card attached!");
        return false;
    }
    
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Type: %d, Size: %lluMB\n", cardType, cardSize);
    
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
    M5.Lcd.setTextSize(2);  // 增大文字大小
    
    // 显示SD卡状态
    M5.Lcd.setCursor(10, 20);  // 垂直居中一些
    M5.Lcd.printf("SD: %s", sdCardReady ? "OK" : "ERROR");
    
    // 显示WiFi状态
    M5.Lcd.setCursor(120, 20);  // 垂直居中一些
    M5.Lcd.printf("WiFi: %s", WiFi.status() == WL_CONNECTED ? "OK" : "OFF");
    
    // 显示录制状态
    M5.Lcd.setCursor(230, 20);  // 垂直居中一些
    M5.Lcd.printf("REC: %s", recording ? "ON" : "OFF");
    
    // 恢复原始文本颜色
    M5.Lcd.setTextColor(currentTextColor);
}

// 新增：创建新的文件名
void createNewFileName() {
    M5.Rtc.GetTime(&RTCtime);
    M5.Rtc.GetDate(&RTCdate);
    
    // 添加随机数以确保文件名唯一
    int randomNum = random(1000);
    
    sprintf(fileName, "/ads_%04d%02d%02d_%02d%02d%02d_%03d.csv",
            RTCdate.Year, RTCdate.Month, RTCdate.Date,
            RTCtime.Hours, RTCtime.Minutes, RTCtime.Seconds, randomNum);
    
    Serial.print("New file name created: ");
    Serial.println(fileName);
}

// 新增：打开数据文件函数
bool openDataFile() {
    // 关闭已打开的文件
    if (dataFile) {
        flushBuffer();
        dataFile.close();
    }
    
    // 重新初始化SD卡
    if (!checkSDCard()) {
        Serial.println("SD Card initialization failed!");
        sdCardReady = false;
        return false;
    }
    
    // 创建新文件名
    createNewFileName();
    
    // 尝试创建新文件
    dataFile = SD.open(fileName, FILE_WRITE);
    if (!dataFile) {
        Serial.println("Failed to create new file!");
        Serial.print("File name: ");
        Serial.println(fileName);
        return false;
    }
    
    // 写入CSV头部
    dataFile.println("Timestamp, Voltage (mV)");
    dataFile.flush();
    dataFile.close();
    
    // 用追加模式重新打开文件
    dataFile = SD.open(fileName, FILE_APPEND);
    if (!dataFile) {
        Serial.println("Error opening file for appending!");
        return false;
    }
    
    Serial.println("File created and opened successfully for recording.");
    return true;
}

void setup(void) {
    M5.begin();             // 初始化M5Core2
    M5.Lcd.setTextSize(2);  // 设置文字大小为2
    
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

    // 创建初始文件名
    createNewFileName();
    
    // 初始化ADS1100
    ads.getAddr_ADS1100(ADS1100_DEFAULT_ADDRESS);
    ads.setGain(GAIN_ONE);  // 1x 增益(default)
    ads.setMode(MODE_CONTIN);  // 连续转换模式 (default)
    ads.setRate(RATE_8);  // 8SPS (default)
    ads.setOSMode(OSMODE_SINGLE);  // 单次转换模式
    ads.begin();  // 初始化硬件

    // 绘制初始界面
    M5.Lcd.fillScreen(BLACK);  // 清屏
    updateStatusDisplay();      // 更新状态栏
    drawStartButton();         // 绘制START按钮
}

void loop(void) {
    M5.update();
    
    // 定期检查SD卡状态
    unsigned long currentMillis = millis();
    if (currentMillis - lastSDCheck >= SD_CHECK_INTERVAL) {
        lastSDCheck = currentMillis;
        if (!checkSDCard()) {
            sdCardReady = false;
            Serial.println("SD Card check failed!");
            M5.Lcd.fillScreen(RED);
            M5.Lcd.setTextColor(WHITE);
            M5.Lcd.drawString("SD Card Error!", 10, 10, 2);
            delay(1000);
            M5.Lcd.fillScreen(BLACK);
            updateStatusDisplay();
            if (!recording) {
                drawStartButton();
            }
        }
    }
    
    // 更新状态显示（现在有1秒的间隔）
    updateStatusDisplay();
    
    // 触摸屏控制
    if (M5.Touch.ispressed()) {
        TouchPoint_t point = M5.Touch.getPressPoint();
        if (!recording && point.x > START_BTN_X && point.x < START_BTN_X + BTN_WIDTH &&
            point.y > BUTTON_AREA_Y && point.y < BUTTON_AREA_Y + BTN_HEIGHT) {
            
            // 尝试打开文件
            if (!openDataFile()) {
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
            startTime = millis();  // 记录开始时间
            clearButtonArea(START_BTN_X, BUTTON_AREA_Y);
            drawStopButton();
            Serial.println("Recording Started");

            delay(100); // 增加防抖动
        } else if (recording && point.x > STOP_BTN_X && point.x < STOP_BTN_X + BTN_WIDTH &&
                   point.y > BUTTON_AREA_Y && point.y < BUTTON_AREA_Y + BTN_HEIGHT) {
            recording = false;
            clearButtonArea(STOP_BTN_X, BUTTON_AREA_Y);
            drawStartButton();
            Serial.println("Recording Stopped");

            // 关闭文件
            if (dataFile) {
                flushBuffer();  // 确保所有数据都已写入
                dataFile.close();
                Serial.println("File closed.");
            }
            delay(100); // 增加防抖动
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

            // 更新LCD显示
            M5.Lcd.fillRect(0, DATA_AREA_Y, 320, DATA_AREA_HEIGHT, BLACK);  // 只清除数据区域
            M5.Lcd.setTextSize(2);  // 设置数据区域文字大小为2
            M5.Lcd.setCursor(10, DATA_AREA_Y + 20);
            M5.Lcd.printf("Voltage: %.3f mV", voltage);
            M5.Lcd.setCursor(10, DATA_AREA_Y + 50);
            M5.Lcd.printf("Time: %s", str_buffer);
            M5.Lcd.setCursor(10, DATA_AREA_Y + 80);
            M5.Lcd.printf("Duration: %lu s", duration);

            // 串口输出
            Serial.print("Timestamp: ");
            Serial.print(str_buffer);
            Serial.print(" | Voltage: ");
            Serial.print(voltage, 6);
            Serial.print(" mV | Duration: ");
            Serial.println(duration);

            // 使用缓冲区写入数据到SD卡
            char dataLine[100];
            sprintf(dataLine, "%s, %.6f\n", str_buffer, voltage);
            addToBuffer(dataLine);
            
            // 每100条数据强制刷新一次
            if (bufferIndex > SD_BUFFER_SIZE * 0.8) {
                flushBuffer();
            }
        }
    }

    // 当停止录制时，确保所有数据都已写入
    if (!recording && dataFile) {
        flushBuffer();
        dataFile.close();
        Serial.println("File closed and all data saved.");
        M5.Lcd.fillScreen(GREEN);
        M5.Lcd.setTextColor(BLACK);
        M5.Lcd.drawString("Data Saved!", 10, 10, 2);
        delay(1000);
        M5.Lcd.fillScreen(BLACK);
    }
}
