// 显示区域定义
#define STATUS_AREA_HEIGHT 60  // 增加状态栏高度
#define DATA_AREA_Y 60        // 减小间隙，直接从状态栏下方开始
#define DATA_AREA_HEIGHT 110  // 稍微增加数据区域高度
#define BUTTON_AREA_Y 180

// 添加新的状态变量
unsigned long lastStatusUpdate = 0;
const unsigned long STATUS_UPDATE_INTERVAL = 1000;  // 状态栏更新间隔（1秒）

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
    
    Serial.println("File created and opened successfully for recording.");
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
} 