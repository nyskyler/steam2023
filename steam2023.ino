#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <DHT.h>
#include <PMS.h>
#include <RtcDS3231.h>
#include <SD.h>

#define OLED_MOSI     6
#define OLED_CLK      5
#define OLED_DC       8
#define OLED_CS       9
#define OLED_RST      7
#define DHTPIN  4
#define SD_CS 53
#define ARRAY_LEN  70
#define INTERVAL_MIN  60
#define TEMP_CALI -1.5  # 보드 발열의 영향을 고려한 기온값 보정치
#define HUMI_CALI 2.0 # 보드 발열의 영향을 고려한 습도값 보정치

Adafruit_SH1106G display = Adafruit_SH1106G(128, 64, OLED_MOSI, OLED_CLK, OLED_DC, OLED_RST, OLED_CS);
DHT dht(DHTPIN, DHT22);
PMS pms(Serial1);
PMS::DATA data;
RtcDS3231<TwoWire> RTC(Wire);
File myFile;

typedef struct { // 기온, 습도, 초미세먼지 데이터를 저장하기 위한 구조체 선언
  float temp;
  float humi;
  float pm2_5;
  boolean writable;
} COLLECTED_DATA;

COLLECTED_DATA * rawData[ARRAY_LEN]; // 구조체 포인터 배열
char fileName[40];
char ymd[40];
char hms[40];
int day_week; 
String dayOfWeek;
String dustCategory;
String sdCheck;
unsigned long previousMillis = 0;
unsigned long interval = 10000;
byte previous_minute = 0;
byte current_minute = 0;
boolean timeElapsed = true;

String fixedWidthStr(int n, int width);
void printDateTime(const RtcDateTime dt);
boolean isTimetoWrite(byte minute);
void arrayInit(void);
void arrayElementInput(byte minute);
//void showStructArray(void);
COLLECTED_DATA findTheMean(void);
boolean isTimeElasped(void);
String findDustCategory(uint16_t pm);
uint8_t min_count = 0; 

static const unsigned char PROGMEM temp_icon[] =
{
  0X00,0X00,0X00,
  0X00,0XE0,0X00,
  0X01,0X10,0X00,
  0X01,0X10,0X00,
  0X01,0X10,0X00,
  0X01,0X10,0X00,
  0X01,0X10,0X00,
  0X01,0X10,0X00,
  0X01,0X50,0X00,
  0X01,0X50,0X00,
  0X01,0X50,0X00,
  0X01,0X50,0X00,
  0X02,0XE8,0X00,
  0X02,0XE8,0X00,
  0X02,0XE8,0X00,
  0X02,0XE8,0X00,
  0X02,0X08,0X00,
  0X01,0XF0,0X00,
  0X00,0X00,0X00,
  0X00,0X00,0X00,
};
static const unsigned char PROGMEM humi_icon[] =
{
  0x00,0x00,0x00,
  0x00,0x30,0x00,
  0x00,0x50,0x00,
  0x00,0x48,0x00,
  0x00,0x88,0x00,
  0x01,0x84,0x00,
  0x01,0x04,0x00,
  0x03,0x02,0x00,
  0x02,0xc3,0x00,
  0x06,0xc9,0x00,
  0x04,0x11,0x00,
  0x04,0x21,0x00,
  0x04,0x4d,0x00,
  0x02,0xcd,0x00,
  0x03,0x02,0x00,
  0x01,0x8c,0x00,
  0x00,0x78,0x00,
  0x00,0x00,0x00,
  0x00,0x00,0x00,
  0x00,0x00,0x00,
};
static const unsigned char PROGMEM PM25_icon[] =
{
  0x00,0x00,0x00,
  0x00,0x00,0x00,
  0x00,0x00,0x00,
  0x30,0x61,0x80,
  0x00,0x00,0x00,
  0x00,0x00,0x00,
  0x02,0x0c,0x00,
  0x02,0x00,0x00,
  0x00,0x00,0x00,
  0x00,0x40,0x80,
  0x30,0x40,0x80,
  0x00,0x00,0x00,
  0x00,0x00,0x00,
  0x06,0x0c,0x00,
  0x00,0x00,0x00,
  0x00,0x00,0x00,
  0x30,0x40,0x80,
  0x00,0x40,0x80,
  0x00,0x00,0x00,
  0x00,0x00,0x00,
};

void setup() {
  Serial.begin(9600);
  Serial1.begin(9600);
  dht.begin();
  display.begin(0, true);
  display.display();
  RTC.Begin();
  
  if (!RTC.GetIsRunning()) {
    Serial.println("restart");
    RTC.SetIsRunning(true);
  }
  
  RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
  RtcDateTime _now = RTC.GetDateTime();
  
  if (_now < compiled) {
    Serial.println("update");
    RTC.SetDateTime(compiled);
  }
  
  if (!SD.begin()) { // SD카드 모듈을 초기화합니다.
    sdCheck = "ERR";
    return;
  } else {
    sdCheck = "REC";
  }
    
  for(int i=0; i<ARRAY_LEN; i++) {
    rawData[i] = new COLLECTED_DATA;
    rawData[i]->temp = 0.0;
    rawData[i]->humi = 0.0;
    rawData[i]->pm2_5 = 0.0;
    rawData[i]->writable = true;
  }
}

void loop() {
  if (pms.read(data)) {
    unsigned long currentMillis = millis();
    
    if (currentMillis - previousMillis >= interval) {      
      previousMillis = currentMillis;
      float h = dht.readHumidity() + HUMI_CALI;
      float c = dht.readTemperature() + TEMP_CALI;
    
      if (isnan(h) || isnan(c)) {
        return;
      }
      
      RtcDateTime _now = RTC.GetDateTime();
      day_week = _now.DayOfWeek();
      
      switch(day_week)
      {
        case 0: dayOfWeek = "SUN"; break;
        case 1: dayOfWeek = "MON"; break;
        case 2: dayOfWeek = "TUE"; break;
        case 3: dayOfWeek = "WED"; break;
        case 4: dayOfWeek = "THU"; break;
        case 5: dayOfWeek = "FRI"; break;
        case 6: dayOfWeek = "SAT"; break;
        default: dayOfWeek = ""; break;
      }
      
      dustCategory = findDustCategory(data.PM_AE_UG_2_5);
  
      byte offset = 7;
      display.clearDisplay();
      
      display.setTextSize(2);
      display.setTextColor(SH110X_WHITE);
  
      display.setCursor(18 + offset, 2);
      display.print(":");
      display.print(c);
      display.print(" C");
      
      display.setCursor(18 + offset, 24);
      display.print(":");
      display.print(h);
      display.print(" %");
      display.setCursor(18 + offset, 46);
      display.print(":");
      display.print(data.PM_AE_UG_2_5);
      display.setTextSize(1);
      display.print("  PM2.5 " + sdCheck);
      display.setCursor(65 + offset, 55);
      display.print(fixedWidthStr(_now.Hour(), 2) + ":");
      display.print(fixedWidthStr(_now.Minute(), 2) + ":");
      display.print(fixedWidthStr(_now.Second(), 2));
      display.drawBitmap(0 + offset, 0, temp_icon, 20, 20, 1);
      display.drawBitmap(0 + offset, 20, humi_icon, 20, 20, 1);
      display.drawBitmap(0 + offset, 40, PM25_icon, 20, 20, 1);
      display.display();
      
      sprintf(fileName, "%d%02d%02d.csv", _now.Year(), _now.Month(), _now.Day());
      
      if(SD.exists(String(fileName))){
        sprintf(ymd, "%d-%02d-%02d", _now.Year(), _now.Month(), _now.Day());
        sprintf(hms, "%02d:%02d:%02d", _now.Hour(), _now.Minute(), _now.Second());
        
        myFile = SD.open(String(fileName), FILE_WRITE);
    
        if(myFile){
          current_minute = _now.Minute();
//          Serial.println(current_minute);
//          Serial.println(previous_minute);
          
          if(isTimeElasped()) { // 최근 기록 시간보다 1분 이상 흘렀다면
            min_count++;
            arrayElementInput(current_minute, c, h, data.PM_AE_UG_2_5);
//            Serial.println(min_count);
//            showStructArray();
//            Serial.println("------------------------------------------");
            
            if(min_count >= INTERVAL_MIN) { // INTERVAL_MIN만큼 분이 흘렀다면
              COLLECTED_DATA average_data = findTheMean();  
              myFile.print(ymd);
              myFile.print(",");
              myFile.print(hms);
              myFile.print(",");
              myFile.print(dayOfWeek);
              myFile.print(",");
              myFile.print(average_data.temp);
              myFile.print(",");
              myFile.print(average_data.humi);
              myFile.print(",");
              myFile.print(average_data.pm2_5);
              myFile.print(",");
              myFile.println(findDustCategory(average_data.pm2_5));
              myFile.close();
              arrayInit();    
              min_count = 0;
            }
            previous_minute = current_minute;
          }
          myFile.close();
        }
      } else {
        myFile = SD.open(String(fileName), FILE_WRITE);
        myFile.print("DATE");
        myFile.print(",");
        myFile.print("TIME");
        myFile.print(",");
        myFile.print("DAY");
        myFile.print(",");
        myFile.print("TEMPERATURE(*C)");
        myFile.print(",");
        myFile.print("HUMIDITY(%)");
        myFile.print(",");
        myFile.print("PM 2.5(ug/m3)");
        myFile.print(",");
        myFile.println("PM 2.5 CATEGORY");
        myFile.close();
       }
    }
  }
}

String fixedWidthStr(int n, int width) {
  String str = "";
  for (int i=0; i<width; i++) {
    int remain = n%10;
    n = n/10;
    str = char(remain + '0') + str;
  }
  return str;
}

void printDateTime(const RtcDateTime dt) {
  Serial.print(fixedWidthStr(dt.Year(), 4) + "년 ");
  Serial.print(fixedWidthStr(dt.Month(), 2) + "월 ");
  Serial.print(fixedWidthStr(dt.Day(), 2) + "일, ");
  Serial.print(fixedWidthStr(dt.Hour(), 2) + "시 ");
  Serial.print(fixedWidthStr(dt.Minute(), 2) + "분 ");
  Serial.println(fixedWidthStr(dt.Second(), 2) + "초 ");
}

void arrayInit(void) {  // rawData 배열의 모든 값을 0으로 초기화
  for(int i=0; i<ARRAY_LEN; i++) {
    // rawData[i] = new COLLECTED_DATA;
    rawData[i]->temp = 0.0;
    rawData[i]->humi = 0.0;
    rawData[i]->pm2_5 = 0.0;
    rawData[i]->writable = true;
  }
}

void arrayElementInput(byte minute, float t, float h, float pm) {  // 구조체 포인터 배열에 값을 저장
  rawData[minute]->temp = t;
  rawData[minute]->humi = h;
  rawData[minute]->pm2_5 = pm;
  rawData[minute]->writable = false;
}

COLLECTED_DATA findTheMean(void) {  // 시간당 데이터의 평균을 구하고 구조체를 반환
  byte cnt = 0;
  COLLECTED_DATA sumData = {0.0, 0.0, 0.0, true};
  
  for (int i=0; i<ARRAY_LEN; i++) {
    if (rawData[i]->writable == true) {
      continue;
    }
    sumData.temp += rawData[i]->temp;
    sumData.humi += rawData[i]->humi;
    sumData.pm2_5 += rawData[i]->pm2_5;
    cnt++;
  }
  
  sumData.temp /= cnt;
  sumData.humi /= cnt;
  sumData.pm2_5 /= cnt;
  sumData.writable = false;
  return sumData;
}

boolean isTimeElasped(void) { // 1분의 시간이 경과했다면 true, 아니면 false를 반환
  boolean myBool = (current_minute == previous_minute)? false : true;
  return myBool;
}

String findDustCategory(uint16_t pm) { // 초미세먼지의 상태 정보를 4단계 문자열로 반환
  String mystr;
  if (pm <= 15) {
    mystr = "Good";
  } else if (pm <= 35) {
    mystr = "Moderate";
  } else if (pm <= 75) {
    mystr = "Unhealthy";
  } else {
    mystr = "Very Unhealthy";
  }
  return mystr;
}

//void showStructArray(void) {
//  int myCnt = 0;
//  for(int i=0; i<ARRAY_LEN; i++) {
//    if (rawData[i]->writable) {
//      continue;
//    }
//    myCnt++;
//    Serial.println(myCnt);
//    Serial.print(rawData[i]->temp);
//    Serial.print(", ");
//    Serial.print(rawData[i]->humi);
//    Serial.print(", ");
//    Serial.println(rawData[i]->pm2_5);
//  }
//}
