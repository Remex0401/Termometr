/***********************************************************
// Konfiguracja Blynk IoT Cloud
************************************************************/
#define BLYNK_TEMPLATE_ID "TMPL4CCpyI19S"
#define BLYNK_TEMPLATE_NAME "Termometr"
#define BLYNK_AUTH_TOKEN   "M0zAG-YnWPr9FhX3R23YHlgzxggjAcIk"

#define BLYNK_PRINT Serial
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Preferences.h>

/***********************************************************
// Biblioteki sensorów i wyświetlacza
************************************************************/
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_AHTX0.h>
#include <TFT_eSPI.h>

/***********************************************************
// Biblioteka do obsługi 433 MHz
************************************************************/
#include <RCSwitch.h>

/***********************************************************
// Biblioteka do zarządzania WiFi (WiFiManager)
************************************************************/
#include <WiFiManager.h>

/***********************************************************
// Obiekty czujników
************************************************************/
Adafruit_BMP280 bmp;
Adafruit_AHTX0 aht; // AHT20

/***********************************************************
// Obiekt ekranu TFT
************************************************************/
TFT_eSPI tft = TFT_eSPI();

/***********************************************************
// Ustawienia I2C (SDA=20, SCL=21)
************************************************************/
#define SDA_PIN 20
#define SCL_PIN 21

/***********************************************************
// Timery i zmienne globalne
************************************************************/
BlynkTimer timer;
Preferences prefs;

// Zmienne na odczyty
float g_tempAHT  = 0;  // Temp wewn. (AHT20)
float g_humidity = 0;  // Wilgotnosc (AHT20)
float g_pressure = 0;  // Cisnienie (BMP280)
float g_tempDS   = 2137;  // Temp zew. (RF)
bool g_firstPacketReceived = false;  // flaga, czy odebrano kiedykolwiek pakiet

float g_altitude = 355.0; // Wysokość n.p.m. zapisywana w Preferences

// Odbiornik 433 MHz
RCSwitch mySwitch = RCSwitch();
#define RX_PIN 2

// Czas ostatniego pakietu z RF
unsigned long g_lastRFMillis = 0;  
#define RF_TIMEOUT_MS 60000 // Po 60s bez nowego pakietu - "Brak połączenia"

// Flaga do sygnalizacji, czy jest WiFi
bool wifiConnected = false;
bool blynkConnected = false;

/***********************************************************
// Rysowanie statusu WiFi
************************************************************/
void drawWifiStatus() {
  int16_t wifiX = 380;
  int16_t wifiY = 120;

  tft.setTextSize(3);

  if (!wifiConnected) {
    tft.setCursor(wifiX, wifiY);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.print("Brak");
    tft.setCursor(wifiX, wifiY+24);
    tft.print("WiFi");
  } else {
    tft.setCursor(wifiX, wifiY);
    tft.fillRect(wifiX, wifiY+24, 100, 24, TFT_BLACK);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.print("WiFi");
  }
}

/***********************************************************
// Obsługa zmiany wysokości z Blynk (np. na V4)
************************************************************/
BLYNK_WRITE(V4) {
  float newAltitude = param.asFloat();
  g_altitude = newAltitude;

  // Zapis w Preferences
  prefs.begin("MojeUstawienia", false);
  prefs.putFloat("altitude", newAltitude);
  prefs.end();

  Serial.print("Zapisano altitude w Preferences: ");
  Serial.println(newAltitude);
}

/***********************************************************
   Funkcja reInitBMP() i reInitAHT()
************************************************************/
bool reInitBMP() {
  Serial.println("reInitBMP(): ponowna inicjalizacja BMP280...");
  if (bmp.begin(0x76)) {
    Serial.println("BMP280 ponownie ruszył na 0x76");
    return true;
  }
  if (bmp.begin(0x77)) {
    Serial.println("BMP280 ponownie ruszył na 0x77");
    return true;
  }
  Serial.println("BMP280 nadal nie odpowiada!");
  return false;
}

bool reInitAHT() {
  Serial.println("reInitAHT(): ponowna inicjalizacja AHT20...");
  if (aht.begin()) {
    Serial.println("AHT20 ponownie ruszył!");
    return true;
  }
  Serial.println("AHT20 nadal nie odpowiada!");
  return false;
}

/***********************************************************
   Funkcja readSensors()
************************************************************/
void readSensors() {
  // --- AHT20 ---
  sensors_event_t tempEvent, humidityEvent;
  aht.getEvent(&tempEvent, &humidityEvent);

  float newTemp = tempEvent.temperature;            
  float newHum  = humidityEvent.relative_humidity;  

  // Zakres -50..+80 i 0..100%
  if (newTemp < -50 || newTemp > 80 || newHum < 0 || newHum > 100) {
    Serial.println("AHT20: Odczyt poza zakresem, reInitAHT...");
    if (!reInitAHT()) {
      Serial.println("AHT20: reInit nieudany, zostawiam starą wartość");
      return;
    } else {
      aht.getEvent(&tempEvent, &humidityEvent);
      newTemp = tempEvent.temperature;
      newHum  = humidityEvent.relative_humidity;
      if (newTemp < -50 || newTemp > 80 || newHum < 0 || newHum > 100) {
        Serial.println("AHT20: wciaz poza zakresem po reInit");
        return;
      }
    }
  }
  // UWAGA: w oryginale było odwrotnie (temp=humidity, hum=temperature),
  // sprawdź, czy to zamierzona zamiana!
  g_humidity = newTemp;
  g_tempAHT  = newHum;

  // --- BMP280 ---
  float newPressure = bmp.readPressure() / 100.0;  // [hPa]
  if (newPressure < 900 || newPressure > 1100) {
    Serial.println("BMP280: Odczyt poza zakresem, reInitBMP...");
    if (!reInitBMP()) {
      Serial.println("BMP280: reInit nieudany, zostawiam starą wartość");
      return;
    } else {
      newPressure = bmp.readPressure() / 100.0;
      if (newPressure < 900 || newPressure > 1100) {
        Serial.println("BMP280: nadal poza zakresem");
        return;
      }
    }
  }
  // Używamy g_altitude z Preferences
  g_pressure = bmp.seaLevelForAltitude(g_altitude, newPressure);

  // --- RF (RCSwitch) ---
  if (mySwitch.available()) {
    long receivedValue = mySwitch.getReceivedValue();
    mySwitch.resetAvailable();
    if (receivedValue != 0) {
      float newTempRF = (float)receivedValue / 100.0;
      if (newTempRF < -50 || newTempRF > 80) {
        Serial.println("RF: Odczyt poza zakresem, ignoruje");
      } else {
        g_tempDS = newTempRF;
        g_firstPacketReceived = true;
        g_lastRFMillis = millis();
        Serial.print("RF: Otrzymano wartosc = ");
        Serial.println(receivedValue);
        Serial.print("Przeliczona temp zew = ");
        Serial.println(g_tempDS);
      }
    } else {
      Serial.println("RF: Kod 0 (niezidentyfikowany).");
    }
  }
}

/***********************************************************
   Funkcja drawStaticLabels()
************************************************************/
void drawStaticLabels() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  tft.setCursor(1, 10);
  tft.print("Dane wewnetrzne:");

  tft.setCursor(1, 110);
  tft.print("Barometr:");

  tft.setCursor(1, 210);
  tft.print("Temperatura zew:");
}

/***********************************************************
   Funkcja updateDynamicData()
************************************************************/
void updateDynamicData() {
  int valueX = 1;      
  int yTemp = 60;      
  int yHum  = 60;      
  int yPres = 160;     
  int yTempZew = 260;  
  int w = 150, h = 40; 

  // Czyścimy tylko obszary z dynamicznymi danymi
  tft.fillRect(valueX, yTemp,    w, h, TFT_BLACK);
  tft.fillRect(valueX, yHum,     w, h, TFT_BLACK);
  tft.fillRect(valueX, yPres,    w, h, TFT_BLACK);
  tft.fillRect(valueX, yTempZew, w, h, TFT_BLACK);

  tft.setTextSize(4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  // AHT20
  tft.setCursor(valueX, yTemp);
  tft.printf("%.2f C", g_tempAHT);

  tft.setCursor(230, yHum);
  tft.printf("%.1f %%", g_humidity);

  // BMP280
  tft.setCursor(valueX, yPres);
  tft.printf("%.1f hPa", g_pressure);

  // RF
  tft.setCursor(valueX, yTempZew);
  unsigned long now = millis();
  if (!g_firstPacketReceived) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.print("Brak polaczenia");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
  } else {
    if ((now - g_lastRFMillis) > RF_TIMEOUT_MS) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.print("Brak polaczenia");
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
    } else {
      tft.printf("%.2f C", g_tempDS);
    }
  }
  drawWifiStatus();
  Serial.println("Dynamic data zaktualizowane");
}

/***********************************************************
   Funkcja updateTFT()
************************************************************/
void updateTFT() {
  readSensors();
  updateDynamicData();
}

/***********************************************************
   Funkcja sendDataBlynk()
************************************************************/
void sendDataBlynk() {
  readSensors();
  Blynk.virtualWrite(V1, g_tempAHT); 
  Blynk.virtualWrite(V0, g_humidity);
  Blynk.virtualWrite(V3, g_pressure);
  Blynk.virtualWrite(V4, g_tempDS);
  Serial.println("Dane wyslane do Blynk");
}

/***********************************************************
   Funkcja setup()
************************************************************/
void setup() {
  Serial.begin(115200);
  delay(1000);

  // 1. Wczytanie altitude z Preferences
  prefs.begin("MojeUstawienia", false);
  float storedAlt = prefs.getFloat("altitude", 355.0);
  g_altitude = storedAlt;
  prefs.end();
  Serial.print("Wczytano altitude z Preferences: ");
  Serial.println(g_altitude);

  // 2. Inicjalizacja I2C
  Wire.setPins(SDA_PIN, SCL_PIN);
  Wire.begin();

  // 3. Inicjalizacja AHT20
  if (!aht.begin()) {
    Serial.println("Nie udalo sie zainicjowac AHT20!");
  }

  // 4. Inicjalizacja BMP280
  if (!bmp.begin(0x76)) {
    Serial.println("BMP280 nie znaleziony na 0x76, proba 0x77...");
    if (!bmp.begin(0x77)) {
      Serial.println("BMP280 nie znaleziony. STOP");
    }
  }
  bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                  Adafruit_BMP280::SAMPLING_X1,
                  Adafruit_BMP280::SAMPLING_X1,
                  Adafruit_BMP280::FILTER_OFF,
                  Adafruit_BMP280::STANDBY_MS_1000);

  // 5. Inicjalizacja TFT
  tft.init();
  tft.setRotation(1);
  drawStaticLabels(); 

  // 6. Inicjalizacja RCSwitch
  mySwitch.enableReceive(RX_PIN);
  mySwitch.setProtocol(1);
  mySwitch.setPulseLength(350);
  Serial.println("Odbiornik RF na pinie: " + String(RX_PIN));

  // 7. Jednorazowe odświeżenie ekranu
  updateTFT();

  // 8. Konfiguracja WiFiManager
  {
    WiFiManager wifiManager;
    // Czekamy 60s na połączenie z zapamiętaną siecią
    wifiManager.setTimeout(60);

    // autoConnect -> jeśli nie uda się w 60s, tworzy AP "Termometr_AP"
    if(!wifiManager.autoConnect("Termometr_AP")) {
      Serial.println("WiFiManager: nie udalo sie polaczyc / skonfigurowac. Pracuje offline.");
      wifiConnected = false;
    } else {
      Serial.println("WiFiManager: polaczono z WiFi!");
      wifiConnected = true;
    }
  }

  // 9. Jeśli WiFi jest OK, łączymy Blynk
  if (wifiConnected) {
    Blynk.begin(BLYNK_AUTH_TOKEN, WiFi.SSID().c_str(), WiFi.psk().c_str());
    if (Blynk.connected()) {
      blynkConnected = true;
      Serial.println("Blynk polaczony!");
    } else {
      Serial.println("Nie udalo sie polaczyc z Blynk (ale WiFi jest) - kontynuujemy offline Blynk");
    }
  }

  // 10. Ustawienie timerów
  timer.setInterval(3000L, updateTFT);      // co 3 sekundy odczyt + TFT
  timer.setInterval(300000L, sendDataBlynk);// co 5 min Blynk

  Serial.println("SETUP zakonczony");
}

/***********************************************************
   Funkcja loop()
************************************************************/
void loop() {
  if (wifiConnected) {
    Blynk.run();
  }
  timer.run();
}
