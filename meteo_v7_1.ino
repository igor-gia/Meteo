#include <Arduino.h>      
#include <WiFi.h>         // Сначала Wi-Fi, так как он зависит от сетевого стека
#include <WiFiUdp.h>      // Затем UDP для NTP (работает с Wi-Fi)
#include <NTPClient.h>    // NTP клиент (зависит от WiFi и WiFiUdp)
#include <WebServer.h>    // Веб-сервер, использующий Wi-Fi
#include <Wire.h>         // I2C (нужно для датчиков, дисплеев и памяти)
#include <Preferences.h>  // EEPROM-like хранилище 
#include <SensirionI2cScd4x.h>  
#include <Adafruit_BME280.h>
#include <TFT_eSPI.h>     // Дисплей (работает с SPI, но не зависит от I2C)
#include <MQTT.h>         
#include "Free_Fonts.h"   // Кастомные шрифты для TFT (не содержит кода)

#define DOUT 39  /* Data out pin (T_DO) of touch screen */
#define DIN  32  /* Data in pin (T_DIN) of touch screen */
#define DCS  33  /* Chip select pin (T_CS) of touch screen */
#define DCLK 25  /* Clock pin (T_CLK) of touch screen */
#include <TFT_Touch.h>
TFT_Touch touch = TFT_Touch(DCS, DCLK, DIN, DOUT);


// Для создания библиотеки из изображения: 
// Сгенерировать файл с изображением https://notisrac.github.io/FileToCArray/
// Code format Hex (0x00)
// Palette mod 16bit RRRRRGGGGGGBBBBB (2byte/pixel)
// Endianness - Big-endian
// Data type uint16_t
#include "template6.h"  //  Сгенерированный файл с изображением
// Преобразуем массив изображения в uint16_t*
const uint16_t *img = (const uint16_t*)template6;

// Пины шины I2C
#define SDA_PIN 27   
#define SCL_PIN 22

SensirionI2cScd4x scd4x;
Adafruit_BME280 bme;

static char errorMessage[64];
static int16_t error;
#define NO_ERROR 0

// Переменные для данных
String currentDate;
String currentTime;
float currentTemperature;
float currentHumidity;
float currentPressure;
int currentAirQuality;

// переменные для чтения данных с SCD40
uint16_t co2;
float scd_temp, scd_humidity;

// объявляем структуру для массивов исторических данных
struct WeatherData {
  short int temperature;  // Температура
  short int humidity;     // Влажность
  short int pressure;     // Давление
  short int airQuality;   // CO2
};

// Объявляем структуру для хранени координат вывода текста и графиков
struct UIElement {
  int x;            // Координата X
  int y;            // Координата Y
  uint16_t fgColor; // цвет текста/графики
};

// значение min и max для рассчета коэффицента масштабирования графика 
int minTemp, maxTemp;         
int minHum, maxHum;
int minPres, maxPres;
int minAirQ, maxAirQ;

const int backlightPin = 21;  // Пин подсветки
const int sensorPin = 34;     // Пин фоторезистора
int backlight;                // значение яркости экрана

const long intervalClock = 1000;        // Интервал обновления показателей даты и времени на экране в миллисекундах (1 сек)
const long intervalSensors = 5000;      // Интервал чтения датчиков и обновления показателей на экране в миллисекундах (5 сек)
const long intervalNTP = 21600000;      // Интервал синхронизации времени с сервером NTP в миллисекундах (21600000 миллисекунд = 6 часов)
const int HISTORY_SIZE = 96;            // Размер массива истории - 96 значений для 2 суток (по 30 минут)
int interval_graph = 80;                // Интервал обновления графиков на экране в секундах
int interval_MQTT = 1;                  // Интервал отправки информации по MQTT в минутах (2 мин = 120)

unsigned long previousMillisClock;
unsigned long previousMillisSensors;
unsigned long previousMillisGraph = 0;
unsigned long previousMillisMQTT = 0;
unsigned long previousMillisNTP = 0;

int dispRot = 3;         // ориентация дисплея: 1 - питание слева, 3- питание справа

WeatherData histogramData[HISTORY_SIZE];     // Массив для хранения усредненных значений для гистограммы - 96 значений для 2 суток (по 30 минут)

// координаты для вывода текста на экран
UIElement tempText = {142, 40, 0xFEA0};   // sRGB: #FFD700
UIElement humText  = {142, 84, 0xACF2};   // sRGB: #A89C94
UIElement presText = {142, 128, 0xFAA4};  // sRGB: #FF5722
UIElement airText  = {142, 172, 0xFA20};  // sRGB: #FF4500

UIElement bigClock = {20, 50, TFT_GREEN};   //  координаты для больших часов в ночном режиме (TL_DATUM)
UIElement bigDate  = {160, 170, TFT_GREEN}; //  координаты для даты в ночном режиме (TC_DATUM)

// координаты для вывода графиков на экран
UIElement tempGraph = {210, 73, 0xFEA0};
UIElement humGraph  = {210, 117, 0xACF2};
UIElement presGraph = {210, 161, 0xFAA4};
UIElement airGraph  = {210, 205, 0xFA20};

// Координаты для строк статусов
const int statusLine1x = 2;
const int statusLine1y = 215;
const int statusLine2x = 2;
const int statusLine2y = 228;

// Цвета состояний в строке статуса
const uint16_t colorOK = 0x528A;
const uint16_t colorLost = 0xFFE0;

const int GRAPH_HEIGHT = 30;        // Высота для всех графиков

int ntpFailCount = 0;         // Счетчик неудач подключения к NTP
const int ntpMaxFails = 6;    // Количество попыток подключиться к NTP, после чего скрыть часы после 6 неудач (3 часа), попытки подключения продолжаются
bool showClock = false;       // Флаг показа часов
bool mqttFail;                // Флаг неудачи подключения к MQTT
bool sensorSCDFail = false;   // Флаг неудачи чтения датчика SCD40
bool sensorBMEFail = false;   // Флаг неудачи чтения датчика BME
int wifiFailCount = 0;        // Счетчик неудач подключения к WiFi
const int wifiMaxFails = 9;   // Количество попыток подключиться к WiFi, после чего запускать AP
bool apModeActive = false;    // false - режим подключения к WiFi, true - режим точки доступа

bool screenTouched = false;     // признак нажатия на экран
bool nightMode = false;         // ночной режим (показ часов крупно) 
bool lastNightMode = false;
bool autoNightMode = false;     // запоминаем предыдущее состояние
bool prevAutoNightMode = false; // Предыдущее значение autoNightMode

bool mqtt_enabled;
bool night_mode_enabled;
String night_start;
String night_end;

// Определяем глобальные цвета
uint16_t bgColor = 0x0000;      // Цвет фона

int currentIndexAvg = 0;       // Индекс для накопления количества элементов для получения средних значений

// Переменные для накопления данных за 30 минут (сырые данные) для последующего деления на currentIndexAvg для получения средних значений
long sumTemperature = 0;
long sumHumidity = 0;
long sumPressure = 0;
long sumAirQuality = 0;

unsigned long lastForceSendMQTT = 0;   // Время последней принудительной отправки по MQTT

// Предыдущие значения датчиков для сравнения с текущими для отправки по MQTT
float lastTemp = -1000;
float lastHumidity = -1000;
float lastPressure = -1000;
float lastAirQuality = -1000;

// Пороги изменений показателй (в абсолютных значениях) для внеочередой отправки по MQTT
float ThresholdTemp;
float ThresholdHumidity;
float ThresholdPressure;
float ThresholdAirQuality;

// Настройка экрана
TFT_eSPI tft = TFT_eSPI();

// Настройка Wi-Fi
String wifi_ssid = "";  // Имя Wi-Fi сети
String wifi_password = "";     // Пароль Wi-Fi
const char* APSSID = "gia-meteo";        // SSID точки доступа
const char* APPassword = "12348765";     // Пароль для точки доступа

// Настройка MQTT
String mqtt_server;
int mqtt_port;
String mqtt_username;
String mqtt_password;

WiFiClient net;
MQTTClient client;

// Настройка NTP
WiFiUDP udp;
NTPClient timeClient(udp, "pool.time.in.ua"); // NTP сервер 
int time_offset; // смещение времени (UTC + 2ч) в минутах (в функцию передавать в секундах)

Preferences prefs;
WebServer server(80);

void setup() {
  Serial.begin(115200);   // Инициализация серийного порта
  loadSettings();         // загружаем переменные из энергонезависимой памяти
  apModeActive = false;
  
  delay (500);
  WiFi.disconnect(true, true);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_MODE_STA); // Режим точки доступа
  
  // Попытка подключения к Wi-Fi, если данные есть
  Serial.print("Connecting to Wi-Fi");
  if (wifi_ssid != "") {
    WiFi.begin(wifi_ssid, wifi_password);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 10) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Connected to Wi-Fi");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      apModeActive = false;
    } else {
      Serial.println("Failed to connect to Wi-Fi");
      apModeActive = true;
    }
  } else {
    Serial.println("No Wi-Fi credentials found. Starting AP...");
    apModeActive = true;
  }
  // Запускаем точку доступа
  if(apModeActive) {
    switchToAPMode();
  }

  timeClient.setTimeOffset(time_offset * 60);  //установка смещения времени относительно UTC в секундах
  timeClient.begin();                          // Инициализация NTP
  timeClient.forceUpdate();                    // обновляем показания точного времени
  showClock = timeClient.isTimeSet();
  ntpFailCount=(showClock? 0 : 1);

  //настройка управления яркостью 
  pinMode(backlightPin, OUTPUT);  // устанваливаем пин продсветки в режим выхода
  analogReadResolution(10);       // настраиваем вход для фоторезистора
  analogSetAttenuation(ADC_0db);  // настройка чувствительности

  Wire.begin(SDA_PIN, SCL_PIN); // инициализация i2c

  // Инициализация SCD40
  scd4x.begin(Wire, 0x62);
  scd4x.startPeriodicMeasurement();

  // Инициализация BME280
  if (!bme.begin(0x76)) {
    Serial.println("Ошибка BME280!");
    sensorBMEFail = true;
  }

  tft.begin();               // Инициализация экрана
  tft.setRotation(dispRot);  // Ориентация экрана
  tft.fillScreen(bgColor);   // Заполняем экран чёрным цветом

  tft.pushImage(0, 0, TEMPLATE6_WIDTH, TEMPLATE6_HEIGHT, img);  // Вывод изображения в координатах (0,0)
  if (nightMode) {
      tft.fillRect(0, 31, 319, 178, bgColor);     //закрашиваем среднюю область для отображения больших часов
      tft.fillRect(0, 0, 319, 29, bgColor);       //закрашиваем верхнюю область для отображения показаний датчиков
  }

  if(mqtt_enabled) {         // Подключение к MQTT
    client.begin(mqtt_server.c_str(), mqtt_port, net);
    client.onMessage(messageReceived);
    client.setWill("homeassistant/sensor/esp32_sensor/availability", "offline", true, 1);
    connectMQTT();
    publishDeviceDiscovery();       // Отправка конфигурации сенсоров
    mqttFail = !client.connected(); // флаг для отображения статуса - проверка статуса подключения  
  }

  // Запуск веб-сервера
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();

  previousMillisClock = millis() - intervalClock;
  previousMillisSensors = millis() - intervalSensors;

  showConnections();    // показываем статусы wifi, ntp, mqtt
}

void loop() {
  
  showClock = showClock && !apModeActive;                  // не показываем часы в режиме AP
  
  if (isDue(previousMillisClock, intervalClock)) {         //обновляем дату и время и выводим их на экран с периодичностью intervalClock (1 сек)
    setBrightness();
    updateNightMode();
    if(showClock) {
      unsigned long epochTime = timeClient.getEpochTime(); // Получение времени в формате Unix
      currentDate = getDate(epochTime);                    // Получаем дату
      currentTime = timeClient.getFormattedTime();         // Получаем текущее время в формате Unix (с секунд с 1 января 1970 года)
      displayClock();  
    } else {
      if(nightMode) {
        tft.fillRect(0, 31, 319, 178, bgColor); // закрашиваем неактуальные показания часов в ночном режиме
      } else {
        tft.fillRect(1, 1, 318, 27, bgColor);   // закрашиваем неактуальные показания часов в дневном режиме
      }
    }
  }
   
  if (isDue(previousMillisSensors, intervalSensors)) {      //обновляем показания датчиков и выводим их на экран с периодичностью intervalSensors (5 сек)
    checkWiFi();               // проверяем статус соединения WiFi, если не подключен, пытаемся переподключиться
    showConnections();         // проверяем статусы wifi, ntp, mqtt и выводим сообщение на экран
    readSensors();             // получаем показания датчиков
    if (mqtt_enabled) sendMQTTData(millis());              // отправка данных по MQTT если включен mqtt 

    // Суммируем данные для вычисления средних значений и инкрементируем индекс
    sumTemperature += currentTemperature;
    sumHumidity += currentHumidity;
    sumPressure += currentPressure;
    sumAirQuality += currentAirQuality;
    currentIndexAvg++;
    displaySensors();
  }

  if (isDue(previousMillisGraph, interval_graph * 1000)) {    //обновляем значения графиков с периодичностью interval_graph *1000
     //вычисляем средние значения
    if (currentIndexAvg > 0) {
      // Вычисление средних значений
      int avgTemperature = round(sumTemperature / (float)currentIndexAvg);
      int avgHumidity = round(sumHumidity / (float)currentIndexAvg);
      int avgPressure = round(sumPressure / (float)currentIndexAvg);
      int avgAirQuality = round(sumAirQuality / (float)currentIndexAvg);
      addToHistory(avgTemperature, avgHumidity, avgPressure, avgAirQuality); // добавляем исторические данные в массив
    }
    // обнуляем переменные для накопления данных с датчиков и счетчик
    sumTemperature = 0;
    sumHumidity = 0;
    sumPressure = 0;
    sumAirQuality = 0;
    currentIndexAvg = 0;

    if (!nightMode) {
        drawGraph(); // отображаем графики на экране
    }    
  }

  if (isDue(previousMillisNTP, intervalNTP)) {               //обновляем значения точного времени - синхронизация с NTP (раз в 6 часов)
    checkNTP();
  }

// тут выполняется код, не зависящий от вывода на экран
  handleTouch();          //отслеживание нажатий на экран
  server.handleClient();  //слушаем web-сервер

  if(mqtt_enabled) {
    mqttFail = !client.connected(); // флаг для отображения статуса - проверка статуса подключения  
    if (!mqttFail) client.loop();   // обрабатываем только если подключено
  }
}



// ****************************************************************************************************************************************
// дальше идут функции
// ****************************************************************************************************************************************

bool isDue(unsigned long &previousMillis, unsigned long interval) {
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    return true;
  }
  return false;
}

String getDate(unsigned long epochTime) {
  // Преобразование времени Unix в дату
  int currentYear = 1970;
  unsigned long days = epochTime / 86400; // Количество дней с 1 января 1970 года

  // Вычисляем год
  while (days >= 365) {
    if (isLeapYear(currentYear)) {
      if (days >= 366) {
        days -= 366;
      } else {
        break;
      }
    } else {
      days -= 365;
    }
    currentYear++;
  }

  // Вычисляем месяц и день
  int monthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (isLeapYear(currentYear)) {
    monthDays[1] = 29; // Февраль в високосный год
  }

  int currentMonth = 0;
  while (days >= monthDays[currentMonth]) {
    days -= monthDays[currentMonth];
    currentMonth++;
  }

  int currentDay = days + 1; // Добавляем 1, т.к. дни начинаются с 1

  // Вычисляем день недели
  const char* weekDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  const char* currentWeekDay = weekDays[(epochTime / 86400 + 4) % 7]; // 1 января 1970 года — четверг

  // Формируем строку с датой
  char dateBuffer[22];
  snprintf(dateBuffer, sizeof(dateBuffer), "%s, %02d/%02d/%04d", currentWeekDay, currentDay, currentMonth + 1, currentYear);
  return String(dateBuffer); // Возвращаем строку
}

bool isLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

// Функция регулировки яркости экрана
void setBrightness() {
  int sensorValue = analogRead(sensorPin);      // Получаем значение освещенности c фоторезистора. 0 - максимальная освещенность, 1023 - полная темнота
  float factor = 0.1;                           // Коэффициент плавности
  int brightness = 255 - pow(sensorValue, factor) * (255 / pow(1023, factor));
  brightness = constrain(brightness, 2, 255);   // Ограничение диапазона
  brightness = (nightMode) ? max(brightness - 80, 1) : brightness;  // в ночном режиме уменьшаем яркость на 80 пунктов
  analogWrite(backlightPin, brightness);        // Устанавливаем яркость. Значение яркости. 0 - нет подсветки, 255 - максимальная яркость
}

void displayClock() {
  tft.setTextDatum(TL_DATUM); 
  tft.setTextPadding(215); 
  if (!nightMode) {
    // отображаем дату и время в верхней строке
    tft.setTextColor(TFT_WHITE, bgColor); 
    tft.setTextSize(1);
    tft.setFreeFont(FSS9);
    tft.drawString(currentDate, 1, 5);
    tft.setFreeFont(FSSB12);
    tft.setTextPadding(100); 
    tft.drawString(currentTime, 220, 4);
  } else {
    // отображаем дату и время в центре
    tft.setTextSize(2);
    tft.setTextColor(bigClock.fgColor, bgColor);
    tft.drawString(currentTime.substring(0, 5), bigClock.x, bigClock.y, 7);
    tft.setTextDatum(TC_DATUM); 
    tft.setTextSize(1);
    tft.setTextColor(bigDate.fgColor, bgColor);
    tft.drawString(currentDate, bigDate.x, bigDate.y, 4);

  }
}

void readSensors() {               // Читаем данные с датчиков
  bool dataReady = false;
  error = scd4x.getDataReadyStatus(dataReady);
  if (error != NO_ERROR) {
      Serial.print("Error trying to execute getDataReadyStatus(): ");
      errorToString(error, errorMessage, sizeof errorMessage);
      Serial.println(errorMessage);
      sensorSCDFail = true;
  }

  if (dataReady) {
      error = scd4x.readMeasurement(co2, scd_temp, scd_humidity);
      if (error != NO_ERROR) {
          Serial.print("Error trying to execute readMeasurement(): ");
          errorToString(error, errorMessage, sizeof errorMessage);
          Serial.println(errorMessage);
          sensorSCDFail = true;
      }
  }

  currentAirQuality = co2;
  currentTemperature = bme.readTemperature();
  currentHumidity = bme.readHumidity();
  currentHumidity = (currentHumidity > 100 ? 100: currentHumidity);           // датчик может возвращать значения больше 100% 
  currentPressure = bme.readPressure() / 100.0F; // hPa
}

void displaySensors() {
  if (!nightMode) {
        tft.setTextPadding(95);
        tft.setTextDatum(TR_DATUM); // Центр текста относительно экрана
        tft.setFreeFont(FSSB18);
        tft.setTextColor(tempText.fgColor, bgColor);
        tft.drawFloat(currentTemperature , 1, tempText.x, tempText.y); // Выводим температуру, 1 знак после точки
        tft.setTextColor(humText.fgColor, bgColor);
        tft.drawFloat(currentHumidity , 1, humText.x, humText.y); // Выводим влажность, 1 знак после точки
        tft.setTextColor(presText.fgColor, bgColor);
        tft.drawFloat(currentPressure , 0, presText.x, presText.y); // Выводим давление, 0 знак после точки
        tft.setTextColor(airText.fgColor, bgColor);
        tft.drawFloat(currentAirQuality , 0, airText.x, airText.y); // Выводим качество воздуха, 0 знак после точки.
    } else {
        tft.setFreeFont(FSSB9);
        tft.setTextPadding(70);
        tft.setTextDatum(TR_DATUM);
        tft.setTextColor(tempText.fgColor, bgColor);
        tft.drawString(String(currentTemperature , 1) + " C", 58, 5); // Выводим температуру, 1 знак после точки
        tft.setTextColor(humText.fgColor, bgColor);
        tft.drawString(String(currentHumidity , 1) + " %", 130, 5); // Выводим влажность, 1 знак после точки
        tft.setTextColor(presText.fgColor, bgColor);
        tft.drawString(String(currentPressure , 0) + " hPa", 220, 5); // Выводим давление, 0 знак после точки
        tft.setTextColor(airText.fgColor, bgColor);
        tft.drawString(String(currentAirQuality) + " ppm", 317, 5); // Выводим качество воздуха, 0 знак после точки.
    }
}

// функция записи значений датчиков с предварительным сдвигом предыдущих и нахождением min и max значений каждого параметра
void addToHistory(int avgTemp, int avgHum, int avgPres, int avgAirQ) {
    // Сдвигаем элементы массива вправо и одновременно пересчитываем min/max
    minTemp = maxTemp = avgTemp;
    minHum = maxHum = avgHum;
    minPres = maxPres = avgPres;
    minAirQ = maxAirQ = avgAirQ;

    for (int i = HISTORY_SIZE - 1; i > 0; i--) {
      histogramData[i] = histogramData[i - 1];
      // Обновляем min/max, НЕ включая удалённое значение
      if (histogramData[i].temperature < minTemp) minTemp = histogramData[i].temperature;
      if (histogramData[i].temperature > maxTemp) maxTemp = histogramData[i].temperature;
      if (histogramData[i].humidity < minHum) minHum = histogramData[i].humidity;
      if (histogramData[i].humidity > maxHum) maxHum = histogramData[i].humidity;
      if (histogramData[i].pressure < minPres) minPres = histogramData[i].pressure;
      if (histogramData[i].pressure > maxPres) maxPres = histogramData[i].pressure;
      if (histogramData[i].airQuality < minAirQ) minAirQ = histogramData[i].airQuality;
      if (histogramData[i].airQuality > maxAirQ) maxAirQ = histogramData[i].airQuality;
    }
    // Вставляем новое значение в начало массива
    histogramData[0] = {avgTemp, avgHum, avgPres, avgAirQ};
}

void handleTouch() {
  static unsigned long lastTouchTime = 0;
  const unsigned long debounceDelay = 300; // миллисекунд между нажатиями

  if (touch.Pressed()) {
    unsigned long now = millis();
    if (now - lastTouchTime > debounceDelay) {
      lastTouchTime = now;
      screenTouched = true;
      updateNightMode();
    }
  }
}

// Функция переключения режимов день/ночь
void updateNightMode() {

  if (night_mode_enabled) {
    unsigned int currentTimeInMinutes = timeToMinutes(currentTime);
    unsigned int nightStartInMinutes = timeToMinutes(night_start);
    unsigned int nightEndInMinutes = timeToMinutes(night_end);

  // Определяем, должен ли быть ночной режим в зависимости от времени
    if (nightStartInMinutes < nightEndInMinutes) {
      autoNightMode = (currentTimeInMinutes >= nightStartInMinutes && currentTimeInMinutes < nightEndInMinutes);
    } else {
      autoNightMode = (currentTimeInMinutes >= nightStartInMinutes || currentTimeInMinutes <= nightEndInMinutes);
    }
  }

  if (screenTouched) {
    nightMode = !nightMode;  // Инвертируем ночной режим
    screenTouched = false;   // Сбрасываем флаг касания экрана
  }

  if (autoNightMode != prevAutoNightMode) {
    prevAutoNightMode = autoNightMode;  // Обновляем предыдущее состояние
    nightMode=autoNightMode;
  }

  // Перерисовываем экран, если режим изменился
  if (nightMode != lastNightMode) {
    lastNightMode = nightMode;  // Обновляем переменную для следующего сравнения
    previousMillisClock = millis() - intervalClock;
    previousMillisSensors = millis() - intervalSensors;
    previousMillisGraph = millis() - interval_graph * 1000;
    if (nightMode) {
      tft.fillRect(0, 31, 319, 178, bgColor);  // Закрашиваем область для ночного режима
      tft.fillRect(0, 0, 319, 29, bgColor);    // Закрашиваем верхнюю панель
    } else {
      tft.pushImage(0, 0, TEMPLATE6_WIDTH, TEMPLATE6_HEIGHT, img);  // Вставляем шаблон для дневного режима
    }
  }
}

// Функция проверки соединения WiFi и попыток переподключиться
void checkWiFi() {
  if (apModeActive) return;

  if (wifiFailCount >= wifiMaxFails) {
    // Если не удается подключиться после заданного количества попыток, переключаемся в режим AP
    Serial.println("Too many failed attempts. Switching to AP mode.");
    apModeActive = true;        // Устанавливаем флаг, что режим AP активирован
    wifiFailCount = wifiMaxFails; // Обеспечиваем, что счетчик не выйдет за пределы
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiFailCount = 0;  // Если подключились, сбрасываем счетчик
    apModeActive = false;
  } else {
    wifiFailCount++;
    WiFi.begin(wifi_ssid, wifi_password);
  }
}

// Функция обновления показания точного времени. Если время не обновляется - скрываем часы
void checkNTP() {
  if (apModeActive) return;

  if (timeClient.update()) {
    ntpFailCount = 0;       // Сбрасываем счетчик, если время обновилось
    showClock = true;       // Показываем часы
  } else {
    ntpFailCount++;         // Увеличиваем счетчик неудач
    if (ntpFailCount >= ntpMaxFails) {
      showClock = false;          // Скрываем часы
      ntpFailCount = ntpMaxFails; // Обеспечиваем, что счетчик не выйдет за пределы
    }
  }
}

// Функция показа статуса сетей Wi-Fi, NTP и MQTT
void showConnections() {
    tft.setFreeFont(NULL);
    tft.setTextPadding(180);
    tft.setTextSize(1);
    tft.setTextDatum(TL_DATUM);
  
    // Если Wi-Fi потерян — не показываем NTP и MQTT
    if (wifiFailCount != 0) {  
      tft.setTextColor(colorLost, bgColor);
      tft.drawString(wifiFailCount == wifiMaxFails ? "WiFi lost! Reboot to setup." : "WiFi lost!", statusLine1x, statusLine1y);
      tft.drawString(" ", statusLine2x, statusLine2y); // удаляем информацию со второй строки статуса
    } else {
    // Wi-Fi статус
      tft.setTextColor(colorOK, bgColor);
      tft.drawString("WiFi OK. IP: " + WiFi.localIP().toString(), statusLine1x, statusLine1y);
      // NTP статус
      tft.setTextColor((ntpFailCount != 0) ? colorLost : colorOK, bgColor);
      tft.drawString((ntpFailCount != 0) ? "NTP Bad" : "NTP OK", statusLine2x, statusLine2y);
      // Статус датчиков
      tft.setTextColor((sensorBMEFail || sensorSCDFail) ? colorLost : colorOK, bgColor);
      tft.drawString((sensorBMEFail || sensorSCDFail) ? "Sensors Bad" : "Sensors OK", statusLine2x + 50, statusLine2y);
      // MQTT статус
      if(mqtt_enabled) {          
        tft.setTextColor((mqttFail) ? colorLost : colorOK, bgColor);
        tft.drawString((mqttFail) ? "MQTT Bad" : "MQTT OK", statusLine2x + 125, statusLine2y);
      }
    }
    if (apModeActive && wifiFailCount == 0) {  // если загрузились в режиме AP, то wifiFailCount будет 0 - показываем настройки подключения - адрес, ssid и пароль
      tft.setFreeFont(NULL);
      tft.setTextPadding(180);
      tft.setTextSize(1);
      tft.setTextDatum(TL_DATUM); 
      tft.setTextColor(TFT_WHITE, bgColor);
      tft.drawString("AP mode. IP: " + WiFi.softAPIP().toString(), statusLine1x, statusLine1y);
      tft.drawString(String("SSID: ") + APSSID +", PW: " + APPassword, statusLine2x, statusLine2y);
    }
}

void switchToAPMode() {
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_MODE_AP);
  WiFi.softAP(APSSID, APPassword); // Запуск точки доступа
  
  Serial.println("AP IP address: ");
  Serial.println(WiFi.softAPIP()); // IP точки доступа
}

void drawGraph() {
    // Закрашиваем область графиков перед перерисовкой
    tft.fillRect(tempGraph.x-1, tempGraph.y-GRAPH_HEIGHT-1, HISTORY_SIZE+2, airGraph.y-tempGraph.y + GRAPH_HEIGHT+2, bgColor);

    for (int i = HISTORY_SIZE - 1; i > 0; i--) { // начинаем с последнего индекса
      // Проверка на деление на ноль
      int tempRange = maxTemp - minTemp;
      int humRange = maxHum - minHum;
      int presRange = maxPres - minPres;
      int airQRange = maxAirQ - minAirQ;

      // Если разница равна нулю, присваиваем безопасное значение
      if (tempRange == 0) tempRange = 1;
      if (humRange == 0) humRange = 1;
      if (presRange == 0) presRange = 1;
      if (airQRange == 0) airQRange = 1;

      // Нормализуем значения с учётом исправленных диапазонов
      int prevTempY = ((histogramData[i].temperature - minTemp) * GRAPH_HEIGHT) / tempRange;
      int currTempY = ((histogramData[i - 1].temperature - minTemp) * GRAPH_HEIGHT) / tempRange;
      int prevHumY = ((histogramData[i].humidity - minHum) * GRAPH_HEIGHT) / humRange;
      int currHumY = ((histogramData[i - 1].humidity - minHum) * GRAPH_HEIGHT) / humRange;
      int prevPresY = ((histogramData[i].pressure - minPres) * GRAPH_HEIGHT) / presRange;
      int currPresY = ((histogramData[i - 1].pressure - minPres) * GRAPH_HEIGHT) / presRange;
      int prevAirQY = ((histogramData[i].airQuality - minAirQ) * GRAPH_HEIGHT) / airQRange;
      int currAirQY = ((histogramData[i - 1].airQuality - minAirQ) * GRAPH_HEIGHT) / airQRange;

      tft.drawLine(tempGraph.x + (HISTORY_SIZE - 1 - i), tempGraph.y - prevTempY, tempGraph.x + (HISTORY_SIZE - i), tempGraph.y - currTempY, tempGraph.fgColor);
      tft.drawLine(humGraph.x + (HISTORY_SIZE - 1 - i), humGraph.y - prevHumY, humGraph.x + (HISTORY_SIZE - i), humGraph.y - currHumY, humGraph.fgColor);
      tft.drawLine(presGraph.x + (HISTORY_SIZE - 1 - i), presGraph.y - prevPresY, presGraph.x + (HISTORY_SIZE - i), presGraph.y - currPresY, presGraph.fgColor);
      tft.drawLine(airGraph.x + (HISTORY_SIZE - 1 - i), airGraph.y - prevAirQY, airGraph.x + (HISTORY_SIZE - i), airGraph.y - currAirQY, airGraph.fgColor);
    }
}

// функция формирования web-страницы
void handleRoot() {

String html =
"<html style='font-family: sans-serif;'>\n"
"<head>\n"
"<meta name='viewport' content='width=device-width, initial-scale=1.0'>\n"
"<style>\n"
"body { max-width: 600px; margin: auto; padding: 10px; background: #f5f5f5; }\n"
"h2 { text-align: center; color: #333; }\n"
"form { background: #fff; padding: 20px; border-radius: 8px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }\n"
".section { margin-bottom: 20px; padding: 15px; background: #eef; border-radius: 6px; }\n"
".section-title { text-align: center; font-weight: bold; font-size: 18px; margin-bottom: 10px; color: #555; }\n"
"table { width: 100%; }\n"
"td { padding: 8px; vertical-align: middle; }\n"
"input[type='text'], input[type='password'], input[type='number'], input[type='time'] { width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 4px; }\n"
"input[type='checkbox'] { transform: scale(1.2); }\n"
"input[type='submit'] { width: 100%; padding: 10px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; font-size: 16px; cursor: pointer; }\n"
"input[type='submit']:hover { background-color: #45a049; }\n"
"</style>\n"
"<script>\n"
// Функция для активации/деактивации полей MQTT
"function toggleMQTTSettings() {\n"
"  var mqttEnabled = document.getElementById('mqtt_enabled').checked;\n"
"  var mqttFields = document.getElementsByClassName('mqtt-field');\n"
"  for (var i = 0; i < mqttFields.length; i++) {\n"
"    mqttFields[i].disabled = !mqttEnabled;\n"
"  }\n"
"}\n"
// Функция для активации/деактивации полей Night Mode
"function toggleNightModeSettings() {\n"
"  var nightEnabled = document.getElementById('night_mode_enabled').checked;\n"
"  document.getElementById('night_start').disabled = !nightEnabled;\n"
"  document.getElementById('night_end').disabled = !nightEnabled;\n"
"}\n"
"</script>\n"
"</head>\n"
"<body onload='toggleMQTTSettings(); toggleNightModeSettings();'>\n"

"<h2>GIA-Meteo Configuration</h2>\n"
"<form action='/save' method='POST' autocomplete='off'>\n"

// Wi-Fi
"<div class='section'>\n"
"<div class='section-title'>Wi-Fi Settings</div>\n"
"<table>\n"
"<tr><td>SSID:</td><td><input type='text' name='wifi_ssid' value='" + wifi_ssid + "'></td></tr>\n"
"<tr><td>Password:</td><td><input type='password' name='wifi_password' value='" + wifi_password + "'></td></tr>\n"
"</table>\n"
"</div>\n"

// MQTT
"<div class='section'>\n"
"<div class='section-title'>MQTT Settings</div>\n"
"<label><input type='checkbox' id='mqtt_enabled' name='mqtt_enabled' value='true' onchange='toggleMQTTSettings();' " + (mqtt_enabled ? "checked" : "") + "> Enable MQTT</label>\n"
"<table>\n"
"<tr><td>Server IP:</td><td><input class='mqtt-field' type='text' name='mqtt_server' value='" + mqtt_server + "'></td></tr>\n"
"<tr><td>Port:</td><td><input class='mqtt-field' type='number' name='mqtt_port' value='" + String(mqtt_port) + "'></td></tr>\n"
"<tr><td>User:</td><td><input class='mqtt-field' type='text' name='mqtt_username' value='" + mqtt_username + "'></td></tr>\n"
"<tr><td>Password:</td><td><input class='mqtt-field' type='password' name='mqtt_password' value='" + mqtt_password + "'></td></tr>\n"
"<tr><td>Sending interval (min):</td><td><input class='mqtt-field' type='number' name='interval_MQTT' value='" + String(interval_MQTT) + "'></td></tr>\n"
"<tr><td>Temperature Threshold (&deg;C):</td><td><input class='mqtt-field' type='number' step='0.01' name='ThresholdTemp' value='" + String(ThresholdTemp) + "'></td></tr>\n"
"<tr><td>Humidity Threshold (%):</td><td><input class='mqtt-field' type='number' step='0.01' name='ThresholdHumidity' value='" + String(ThresholdHumidity) + "'></td></tr>\n"
"<tr><td>Pressure Threshold (hPa):</td><td><input class='mqtt-field' type='number' step='0.01' name='ThresholdPressure' value='" + String(ThresholdPressure) + "'></td></tr>\n"
"<tr><td>CO&#8322; Threshold (ppm):</td><td><input class='mqtt-field' type='number' step='0.01' name='ThresholdAirQuality' value='" + String(ThresholdAirQuality) + "'></td></tr>\n"
"</table>\n"
"</div>\n"

// Night Mode
"<div class='section'>\n"
"<div class='section-title'>Night Mode Settings</div>\n"
"<label><input type='checkbox' id='night_mode_enabled' name='night_mode_enabled' value='true' onchange='toggleNightModeSettings();' " + (night_mode_enabled ? "checked" : "") + "> Enable Night Mode</label>\n"
"<table>\n"
"<tr><td>Start Time:</td><td><input type='time' id='night_start' name='night_start' value='" + night_start + "'></td></tr>\n"
"<tr><td>End Time:</td><td><input type='time' id='night_end' name='night_end' value='" + night_end + "'></td></tr>\n"
"</table>\n"
"</div>\n"

// Other Settings
"<div class='section'>\n"
"<div class='section-title'>Other Settings</div>\n"
"<table>\n"
"<tr><td>UTC offset (min):</td><td><input type='number' name='time_offset' value='" + String(time_offset) + "'></td></tr>\n"
"<tr><td>Graph interval (sec/point):</td><td><input type='number' name='interval_graph' value='" + String(interval_graph) + "'></td></tr>\n"
"<tr><td>Display Orientation:</td><td>\n"
"<div style='display:flex; justify-content:space-around;'>\n"
"<label><input type='radio' name='dispRot' value='1' " + (dispRot == 1 ? "checked" : "") + "> Normal</label>\n"
"<label><input type='radio' name='dispRot' value='3' " + (dispRot == 3 ? "checked" : "") + "> Rotated</label>\n"
"</div>\n"
"</td></tr>\n"
"</table>\n"
"</div>\n"

// Submit
"<input type='submit' value='Save & Reboot'>\n"

"</form>\n"
"</body>\n"
"</html>";

  server.send(200, "text/html", html);
}

// функция формирования обработки "update" web-страницы
void handleSave() {
  // Получаем параметры с веб-страницы в постоянной памяти
  wifi_ssid = server.arg("wifi_ssid");
  wifi_password = server.arg("wifi_password");
  mqtt_server = server.arg("mqtt_server");
  mqtt_port = server.arg("mqtt_port").toInt();
  mqtt_username = server.arg("mqtt_username");
  mqtt_password = server.arg("mqtt_password");

  ThresholdTemp = server.arg("ThresholdTemp").toFloat();
  ThresholdHumidity = server.arg("ThresholdHumidity").toFloat();
  ThresholdPressure = server.arg("ThresholdPressure").toFloat();
  ThresholdAirQuality = server.arg("ThresholdAirQuality").toFloat();

  interval_MQTT = server.arg("interval_MQTT").toInt();
  time_offset = server.arg("time_offset").toInt();
  interval_graph = server.arg("interval_graph").toInt();
  dispRot = server.arg("dispRot").toInt();
  
  mqtt_enabled = server.hasArg("mqtt_enabled");
  night_mode_enabled = server.hasArg("night_mode_enabled");
  night_start = server.arg("night_start");
  night_end = server.arg("night_end");

  // Сохраняем все параметры в постоянной памяти
  prefs.begin("config", false); 
  prefs.putString("wifi_ssid", wifi_ssid);
  prefs.putString("wifi_password", wifi_password);
  prefs.putBool("mqtt_enabled", mqtt_enabled);
  prefs.putBool("nm_enabled", night_mode_enabled);
  if (mqtt_enabled) {
    prefs.putString("mqtt_server", mqtt_server);
    prefs.putInt("mqtt_port", mqtt_port);
    prefs.putString("mqtt_username", mqtt_username);
    prefs.putString("mqtt_password", mqtt_password);
    prefs.putFloat("ThTemp", ThresholdTemp);
    prefs.putFloat("ThHumidity", ThresholdHumidity);
    prefs.putFloat("ThPressure", ThresholdPressure);
    prefs.putFloat("ThAirQ", ThresholdAirQuality);
    prefs.putInt("interval_MQTT", interval_MQTT);
  }
  if (night_mode_enabled) {
    prefs.putString("night_start", night_start);
    prefs.putString("night_end", night_end);
  }
  prefs.putInt("time_offset", time_offset);
  prefs.putInt("interval_graph", interval_graph);
  prefs.putInt("dispRot", dispRot);
  
  prefs.end();
  server.send(200, "text/html", 
    "<html><head>"
    "<meta http-equiv='refresh' content='5; url=/' />"
    "<style>"
    "body { font-family: sans-serif; text-align: center; padding-top: 50px; }"
    "</style>"
    "</head><body>"
    "<h1>Settings Saved!</h1>"
    "<p>Rebooting, please wait...</p>"
    "</body></html>"
  );
  delay(1000);

  WiFi.mode(WIFI_OFF);     // Полностью отключаем Wi-Fi
  WiFi.disconnect();       // Удаляем все сети и сбрасываем Wi-Fi настройки

  ESP.restart();   // перезагрузка
}

// Функция загрузки всех настроек
void loadSettings() {
  prefs.begin("config", false);  // Открываем пространство "config"
  // Wi-Fi
  wifi_ssid = prefs.getString("wifi_ssid", "");
  wifi_password = prefs.getString("wifi_password", "");
  // MQTT
  mqtt_server = prefs.getString("mqtt_server", "");
  mqtt_port = prefs.getInt("mqtt_port", 1883);
  mqtt_username = prefs.getString("mqtt_username", "");
  mqtt_password = prefs.getString("mqtt_password", "");
  interval_MQTT = prefs.getInt("interval_MQTT", 2);   // если нет такого ключа, значение будет 2 минуты
  ThresholdTemp = prefs.getFloat("ThTemp", 1.0);
  ThresholdHumidity = prefs.getFloat("ThHumidity", 1.0);
  ThresholdPressure = prefs.getFloat("ThPressure", 1.0);
  ThresholdAirQuality = prefs.getFloat("ThAirQ", 1.0);

  mqtt_enabled = prefs.getBool("mqtt_enabled", true);        // по умолчанию MQTT включен
  night_mode_enabled = prefs.getBool("nm_enabled", false);  // по умолчанию ночной режим выключен
  night_start = prefs.getString("night_start", "22:00");     // дефолт - с 22:00
  night_end = prefs.getString("night_end", "07:00");         // дефолт - до 07:00

  // Прочее
  time_offset = prefs.getInt("time_offset", 0);
  interval_graph = prefs.getInt("interval_graph", 1);
  dispRot = prefs.getInt("dispRot", 1);
  prefs.end();  // Закрываем пространство

}

// Отправка конфигурации устройства в Home Assistant
void publishDeviceDiscovery() {
  // Описание устройства
  String deviceInfo = "\"device\": {"
                      "\"identifiers\": [\"esp32_sensor\"],"
                      "\"name\": \"ESP32 Sensor\","
                      "\"manufacturer\": \"DIY\","
                      "\"model\": \"ESP32 Weather Station\","
                      "\"sw_version\": \"1.4\""
                      "}";

  String deviceSettings =  "\"availability_topic\": \"homeassistant/sensor/esp32_sensor/availability\","
                           "\"payload_available\": \"online\","
                           "\"payload_not_available\": \"offline\",";

  // Конфигурация для температуры
  String configTempTopic = "homeassistant/sensor/esp32_temperature/config";
  String configTempPayload = "{\"name\": \"Temperature\","
                             "\"unique_id\": \"esp32_temperature\","
                             "\"device_class\": \"temperature\","
                             "\"unit_of_measurement\": \"°C\","
                             "\"state_topic\": \"homeassistant/sensor/esp32_temperature/state\","
                             "\"value_template\": \"{{ value_json.temperature }}\","
                             + deviceSettings + deviceInfo + "}";

  // Конфигурация для влажности
  String configHumTopic = "homeassistant/sensor/esp32_humidity/config";
  String configHumPayload = "{\"name\": \"Humidity\","
                            "\"unique_id\": \"esp32_humidity\","
                            "\"device_class\": \"humidity\","
                            "\"unit_of_measurement\": \"%\","
                            "\"state_topic\": \"homeassistant/sensor/esp32_humidity/state\","
                            "\"value_template\": \"{{ value_json.humidity }}\","
                            + deviceSettings + deviceInfo + "}";

  // Конфигурация для давления
  String configPresTopic = "homeassistant/sensor/esp32_pressure/config";
  String configPresPayload = "{\"name\": \"Pressure\","
                          "\"unique_id\": \"esp32_pressure\","
                          "\"device_class\": \"pressure\","
                          "\"unit_of_measurement\": \"hPa\","
                          "\"state_topic\": \"homeassistant/sensor/esp32_pressure/state\","
                          "\"value_template\": \"{{ value_json.pressure }}\","
                          + deviceSettings + deviceInfo + "}";

  // Конфигурация для качества воздуха с описанием устройства
  String configAirQTopic = "homeassistant/sensor/esp32_air_quality/config";
  String configAirQPayload = "{\"name\": \"Carbon Dioxide\","
                            "\"unique_id\": \"esp32_air_quality\","
                            "\"device_class\": \"carbon_dioxide\","
                            "\"unit_of_measurement\": \"ppm\","
                            "\"state_topic\": \"homeassistant/sensor/esp32_air_quality/state\","
                            "\"value_template\": \"{{ value_json.air_quality }}\","
                            + deviceSettings + deviceInfo + "}";


  // Публикация конфигурации сенсоров
  client.publish(configTempTopic.c_str(), configTempPayload.c_str(), true, 1);
  client.publish(configHumTopic.c_str(), configHumPayload.c_str(), true, 1);
  client.publish(configPresTopic.c_str(), configPresPayload.c_str(), true, 1);
  client.publish(configAirQTopic.c_str(), configAirQPayload.c_str(), true, 1);

}

// проверка и отправка данных по MQTT если данные изменились или прошло время interval_MQTT (в минутах)
void sendMQTTData(unsigned long timestamp) {
  if (mqttFail) {
    connectMQTT();
  } else {                    // отправка по изменениям датчиков
     if (abs(currentTemperature - lastTemp) >= ThresholdTemp) {
        publishSensorData("temperature", currentTemperature, 1);
        lastTemp = currentTemperature;
    }
    if (abs(currentHumidity - lastHumidity) >= ThresholdHumidity) {
        publishSensorData("humidity", currentHumidity, 1);
        lastHumidity = currentHumidity;
    }
    if (abs(currentPressure - lastPressure) >= ThresholdPressure) {
        publishSensorData("pressure", currentPressure, 0);
        lastPressure = currentPressure;
    }
    if (abs(currentAirQuality - lastAirQuality) >= ThresholdAirQuality) {
        publishSensorData("air_quality", currentAirQuality, 0);
        lastAirQuality = currentAirQuality;
    }

    // Принудительная отправка по настроенному интервалу
    if (timestamp - lastForceSendMQTT >= interval_MQTT * 60000) {
      publishSensorData("temperature", currentTemperature, 1);
      publishSensorData("humidity", currentHumidity, 1);
      publishSensorData("pressure", currentPressure, 0);
      publishSensorData("air_quality", currentAirQuality, 0);

      lastTemp = currentTemperature;
      lastHumidity = currentHumidity;
      lastPressure = currentPressure;
      lastAirQuality = currentAirQuality;

      lastForceSendMQTT = timestamp;
    }
  }
}

// Публикация показания датчика sensor = {"temperature", "humidity", "pressure", "air_quality"}, value - значение, numDecimals - кол-во знаков после запятой
void publishSensorData(String sensor, float value, int numDecimals) {
  String stateTopic = "homeassistant/sensor/esp32_" + sensor + "/state";
  String payload = "{\"" + sensor + "\": " + String(value, numDecimals) + "}";
  client.publish(stateTopic.c_str(), payload.c_str(), true, 1);
}

// Обработчик входящих MQTT-сообщений
void messageReceived(String &topic, String &payload) {
  Serial.println("Получено сообщение [");
  Serial.println(topic);
  Serial.println("]: ");
  Serial.println(payload);
}

// Функция подключения к MQTT
void connectMQTT() {
    client.connect("ESP32Client", mqtt_username.c_str(), mqtt_password.c_str());
    client.publish("homeassistant/sensor/esp32_sensor/availability", "online", true, 1);
}

// Функция, преобразующая строку с часами и минутами тип HH:MM в общее количество минут
unsigned int timeToMinutes(const String& timeStr) {
  int hour = timeStr.substring(0, 2).toInt();  // Извлекаем часы
  int minute = timeStr.substring(3, 5).toInt(); // Извлекаем минуты
  return hour * 60 + minute;
}