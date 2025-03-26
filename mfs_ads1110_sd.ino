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

void drawStartButton() {
    M5.Lcd.fillRect(START_BTN_X, START_BTN_Y, BTN_WIDTH, BTN_HEIGHT, GREEN);
    M5.Lcd.setTextColor(BLACK);
    M5.Lcd.drawCentreString("START", START_BTN_X + BTN_WIDTH / 2, START_BTN_Y + 15, 2);
}

void drawStopButton() {
    M5.Lcd.fillRect(STOP_BTN_X, STOP_BTN_Y, BTN_WIDTH, BTN_HEIGHT, RED);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.drawCentreString("STOP", STOP_BTN_X + BTN_WIDTH / 2, STOP_BTN_Y + 15, 2);
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

void setup(void) {
    M5.begin();             // 初始化M5Core2
    M5.Lcd.setTextSize(2);  // 设置文字大小为2
    drawStartButton();      // 仅绘制START按钮

    // 连接Wi-Fi并同步时间
    connectWiFi();
    if (WiFi.status() == WL_CONNECTED) {
        syncTimeWithNTP();
    } else {
        Serial.println("WiFi connection failed, using RTC time.");
    }

    // SD卡初始化
    if (!SD.begin()) {
        Serial.println("SD Card initialization failed!");
        return;
    }
    Serial.println("SD Card initialized.");

    // 创建数据文件
    M5.Rtc.GetTime(&RTCtime);
    M5.Rtc.GetDate(&RTCdate);
    sprintf(fileName, "/ads1110_data_%04d%02d%02d_%02d%02d%02d.csv",
            RTCdate.Year, RTCdate.Month, RTCdate.Date,
            RTCtime.Hours, RTCtime.Minutes, RTCtime.Seconds);
    dataFile = SD.open(fileName, FILE_WRITE);
    if (dataFile) {
        dataFile.println("Timestamp, Voltage (mV)");
        dataFile.flush();
        dataFile.close();
        Serial.println("File created successfully.");
    } else {
        Serial.println("Failed to create file.");
    }

    // 初始化ADS1100
    ads.getAddr_ADS1100(ADS1100_DEFAULT_ADDRESS);
    ads.setGain(GAIN_ONE);  // 1x 增益(default)
    ads.setMode(MODE_CONTIN);  // 连续转换模式 (default)
    ads.setRate(RATE_8);  // 8SPS (default)
    ads.setOSMode(OSMODE_SINGLE);  // 单次转换模式
    ads.begin();  // 初始化硬件
}

void loop(void) {
    M5.update();
    
    // 触摸屏控制
    if (M5.Touch.ispressed()) {
        TouchPoint_t point = M5.Touch.getPressPoint();
        if (!recording && point.x > START_BTN_X && point.x < START_BTN_X + BTN_WIDTH &&
            point.y > START_BTN_Y && point.y < START_BTN_Y + BTN_HEIGHT) {
            recording = true;
            startTime = millis();  // 记录开始时间
            clearButtonArea(START_BTN_X, START_BTN_Y);
            drawStopButton();
            Serial.println("Recording Started");

            // 打开文件以附加模式写入
            dataFile = SD.open(fileName, FILE_WRITE);
            if (!dataFile) {
                Serial.println("Error opening file for recording!");
            }

            delay(100); // 增加防抖动
        } else if (recording && point.x > STOP_BTN_X && point.x < STOP_BTN_X + BTN_WIDTH &&
                   point.y > STOP_BTN_Y && point.y < STOP_BTN_Y + BTN_HEIGHT) {
            recording = false;
            clearButtonArea(STOP_BTN_X, STOP_BTN_Y);
            drawStartButton();
            Serial.println("Recording Stopped");

            // 关闭文件
            if (dataFile) {
                dataFile.close();
                Serial.println("File closed.");
            }
            delay(100); // 增加防抖动
        }
    }

    // 仅在录制状态下更新数据
    if (recording) {
        unsigned long currentMillis = millis();
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
            M5.Lcd.fillRect(0, 0, 320, 180, BLACK); // 清除数据区域
            M5.Lcd.setCursor(10, 30);
            M5.Lcd.printf("Voltage: %.3f mV", voltage);
            M5.Lcd.setCursor(10, 60);
            M5.Lcd.printf("Timestamp: %s", str_buffer);
            M5.Lcd.setCursor(10, 90);
            M5.Lcd.printf("Duration: %lu seconds", duration);

            // 串口输出
            Serial.print("Timestamp: ");
            Serial.print(str_buffer);
            Serial.print(" | Voltage: ");
            Serial.print(voltage, 6);
            Serial.print(" mV | Duration: ");
            Serial.println(duration);

            // SD卡数据保存
            if (dataFile) {
                dataFile.print(str_buffer);
                dataFile.print(", ");
                dataFile.println(voltage, 6);
                dataFile.flush();
                Serial.println("Data written to SD card.");
            } else {
                Serial.println("Error writing to file!");
            }
        }
    }
}
