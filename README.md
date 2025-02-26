# ESP32 Project

This project is designed for ESP32 using the Arduino IDE. It collects data from sensors and sends it via MQTT.

## Features
- Reads data from BME280 and SCD40 sensors.
- Stores sensor data for a configurable period (default 48 hours).
- Sends data to MQTT broker at configurable intervals (including forced messages).
- Displays real-time readings and historical graphs on an ILI9241 screen.
- When Wi-Fi connection fails, the device switches to Access Point mode.
- After connecting to the Access Point, all settings can be configured through a web interface.
- After configuration (including Wi-Fi settings), the device will reboot and connect to the configured Wi-Fi network.
- Device IP address is shown on the screen for easy access to the web interface.

## Required Libraries
To use this project, you need to install the following libraries:

- **Arduino.h** (for basic Arduino functions)
- **WiFi.h** (for Wi-Fi communication)
- **WiFiUdp.h** (for UDP communication used by NTP)
- **NTPClient.h** (for NTP time synchronization)
- **WebServer.h** (for web server functionality)
- **Wire.h** (for I2C communication with sensors, displays, and memory)
- **Preferences.h** (for EEPROM-like storage)
- **SensirionI2cScd4x.h** (for SCD40 sensor)
- **Adafruit_BME280.h** (for BME280 sensor)
- **TFT_eSPI.h** (for TFT display, works with SPI)
- **MQTT.h** (for MQTT communication)
- **Free_Fonts.h** (for custom fonts for TFT display)

You can install these libraries directly from the Arduino Library Manager:
1. Open Arduino IDE.
2. Go to **Sketch** → **Include Library** → **Manage Libraries**.
3. Search for the library name and click **Install**.

## Installation
1. Clone this repository.
2. Open the project in Arduino IDE.
3. Install the required libraries listed above.
4. Upload the code to your ESP32 board.

## Usage
1. Once the code is uploaded, the device will try to connect to a pre-configured Wi-Fi network.
2. If the Wi-Fi connection fails, the device will switch to Access Point mode.
3. The device's IP address will be displayed on the screen.
4. Connect to the Access Point and open the web interface by entering the displayed IP address in a web browser.
5. Configure the Wi-Fi settings, MQTT parameters, data storage duration, and MQTT message sending intervals through the web interface.
6. After the configuration is complete, the device will reboot and connect to the configured Wi-Fi network.
7. The device's IP address will continue to be shown on the screen and can be used for further access to the web interface.

## License
This project is licensed under the MIT License – see the [LICENSE.md](LICENSE.md) file for details.

## Donations

If you find this project useful and would like to support its development, you can donate via any of the following methods:

- **PayPal**: [gia@gia.org.ua] [Donate via PayPal](https://www.paypal.me)  
- **Ko-fi**: [Donate on Ko-fi](https://ko-fi.com/igorgimelfarb)  

**Your support is greatly appreciated!**


---

# Проект на ESP32

Этот проект предназначен для работы с ESP32 и использует Arduino IDE. Он считывает данные с датчиков и отправляет их через MQTT.

## Возможности
- Чтение данных с датчиков BME280 и SCD40.
- Хранение данных за настраиваемый период (по умолчанию 48 часов).
- Отправка данных на MQTT-сервер с настраиваемым интервалом (включая принудительную отправку сообщений).
- Отображение текущих показателей и исторических графиков на экране ILI9241.
- При невозможности подключения к Wi-Fi устройство переключается в режим точки доступа.
- После подключения к точке доступа можно настроить все параметры через веб-интерфейс.
- После настройки (включая параметры подключения к Wi-Fi) устройство перезагружается и подключается к сети.
- IP-адрес устройства отображается на экране для удобства доступа к веб-интерфейсу.

## Необходимые библиотеки
Для использования этого проекта вам нужно установить следующие библиотеки:

- **Arduino.h** (основные функции Arduino)
- **WiFi.h** (для Wi-Fi связи)
- **WiFiUdp.h** (для UDP-соединения, используемого в NTP)
- **NTPClient.h** (для синхронизации времени с NTP)
- **WebServer.h** (для работы с веб-сервером)
- **Wire.h** (для связи по I2C с датчиками, дисплеями и памятью)
- **Preferences.h** (для хранения данных, похожего на EEPROM)
- **SensirionI2cScd4x.h** (для датчика SCD40)
- **Adafruit_BME280.h** (для датчика BME280)
- **TFT_eSPI.h** (для работы с TFT дисплеем, поддерживает SPI)
- **MQTT.h** (для работы с MQTT)
- **Free_Fonts.h** (для кастомных шрифтов для TFT дисплея)

Вы можете установить эти библиотеки через Менеджер библиотек в Arduino IDE:
1. Откройте Arduino IDE.
2. Перейдите в **Скетч** → **Подключить библиотеку** → **Управление библиотеками**.
3. Найдите нужную библиотеку и нажмите **Установить**.

## Установка
1. Клонируйте этот репозиторий.
2. Откройте проект в Arduino IDE.
3. Установите необходимые библиотеки, указанные выше.
4. Прошейте код в свою плату ESP32.

## Лицензия
Этот проект лицензирован по лицензии MIT — см. файл [LICENSE.md](LICENSE.md) для подробностей.

##  Поддержка
Если вы находите этот проект полезным и хотите поддержать его разработку, вы можете пожертвовать через:

- **PayPal**: [gia@gia.org.ua] [Donate via PayPal](https://www.paypal.me)  
- **Ko-fi**: [Donate on Ko-fi](https://ko-fi.com/igorgimelfarb)  


**Ваша поддержка будет очень ценна!**
