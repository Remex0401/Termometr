// ========================
// Konfiguracja Blynk IoT Cloud
// ========================
#define BLYNK_TEMPLATE_ID "TMPL4CCpyI19S"
#define BLYNK_TEMPLATE_NAME "Termometr"
#define BLYNK_AUTH_TOKEN   "M0zAG-YnWPr9FhX3R23YHlgzxggjAcIk"

#define BLYNK_PRINT Serial
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>

// ========================
// Biblioteki sensorów i wyświetlacza
// ========================
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_AHTX0.h>
#include <TFT_eSPI.h>

// ========================
// Biblioteka do obsługi 433 MHz
// ========================
#include <RCSwitch.h>

// ========================
// Obiekty czujników
// ========================
Adafruit_BMP280 bmp;
Adafruit_AHTX0 aht; // AHT20

// ========================
// Obiekt ekranu TFT
// ========================
TFT_eSPI tft = TFT_eSPI();

// ========================
// Ustawienia I2C (SDA=20, SCL=21)
// ========================
#define SDA_PIN 20
#define SCL_PIN 21

// ========================
// Timery i zmienne globalne
// ========================
BlynkTimer timer;

// Odczyty
float g_tempAHT  = 0;  // Temp wewn. (AHT20)
float g_humidity = 0;  // Wilgotnosc (AHT20)
float g_pressure = 0;  // Cisnienie (BMP280)
float g_tempDS   = 2137;  // Temp zew. (RF)
bool g_firstPacketReceived = false;  // flaga, czy odebrano kiedykolwiek pakiet

// Odbiornik 433 MHz
RCSwitch mySwitch = RCSwitch();
#define RX_PIN 2

// Czas ostatniego pakietu z RF
unsigned long g_lastRFMillis = 0;  
// Po 60s bez nowego pakietu - "Brak połączenia"
#define RF_TIMEOUT_MS 60000

// Flaga do sygnalizacji, czy jest WiFi
bool wifiConnected = false;
bool blynkConnected = false;

/***********************************************************
   Funkcja drawWifiStatus():
   - Rysuje napis "WiFi" w kolorze niebieskim i przekreśla,
     jeśli wifiConnected == false
   - Jeśli wifiConnected == true, wyświetla "WiFi OK" w zielonym
************************************************************/
void drawWifiStatus() {
  // Pozycja, w której chcemy rysować napis
  int16_t wifiX = 380;
  int16_t wifiY = 120;

  // Rozmiar czcionki
  tft.setTextSize(3);

  if (!wifiConnected) {
    // WiFi niepodłączone: napis "WiFi" w kolorze niebieskim z przekreśleniem
    tft.setCursor(wifiX, wifiY);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    String textb = "Brak";
    String textw = "WiFi";
    tft.print(textb);
    tft.setCursor(wifiX, wifiY+24);
    tft.print(textw);

  } else {
    // WiFi podłączone: napis "WiFi OK" w kolorze zielonym (bez przekreślenia)
    tft.setCursor(wifiX, wifiY);
    tft.fillRect(wifiX, wifiY+24, 100, 24, TFT_BLACK);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.print("WiFi");
  }
}

/***********************************************************
   Funkcja reInitBMP() i funkcja do resetu ATH
   Próbuje ponownie zainicjalizować BMP280, jeśli wystąpił błąd
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
   - Odczyt AHT20
   - Odczyt BMP280 (z re-inicjalizacją jeśli <690 hPa)
   - Odczyt RF (z obsługą g_lastRFMillis)
************************************************************/
void readSensors() {
  // --- AHT20 ---
  sensors_event_t tempEvent, humidityEvent;
  aht.getEvent(&tempEvent, &humidityEvent);

  float newTemp = tempEvent.temperature;            // temperatura
  float newHum  = humidityEvent.relative_humidity;  // wilgotność

  // Sprawdzamy, czy odczyty mieszczą się w założonym zakresie
  if (newTemp < -50 || newTemp > 80 || newHum < 0 || newHum > 100) {
    Serial.println("AHT20: Odczyt poza zakresem, reInitAHT...");
    if (!reInitAHT()) {
      Serial.println("AHT20: reInit nieudany, zostawiam starą wartość / lub ERR");
      // Możesz zostawić g_tempAHT, g_humidity bez zmian albo ustawić je na NAN
      return;
    } else {
      // Spróbuj ponownie odczytać po udanej re-inicjalizacji
      aht.getEvent(&tempEvent, &humidityEvent);
      newTemp = tempEvent.temperature;
      newHum  = humidityEvent.relative_humidity;
      // Sprawdź ponownie zakres
      if (newTemp < -50 || newTemp > 80 || newHum < 0 || newHum > 100) {
        Serial.println("AHT20: wciąż poza zakresem po reInit, zostawiam starą wartość / ERR");
        return;
      }
    }
  }
  // Jeśli dotarliśmy tu, newTemp i newHum są poprawne
  g_humidity = newTemp;
  g_tempAHT = newHum;

  // --- BMP280 ---
  float newPressure = bmp.readPressure() / 100.0;  // [hPa]
  if (newPressure < 900 || newPressure > 1100) {
    Serial.println("BMP280: Odczyt poza zakresem, reInitBMP...");
    if (!reInitBMP()) {
      Serial.println("BMP280: reInit nieudany, zostawiam starą wartość / ERR");
      return;
    } else {
      // Ponowny odczyt
      newPressure = bmp.readPressure() / 100.0;
      if (newPressure < 900 || newPressure > 1100) {
        Serial.println("BMP280: nadal poza zakresem, zostawiam starą wartość / ERR");
        return;
      }
    }
  }

  float altitude = 355;  // wysokosc n.p.m. (docelowo z Blynk)
  g_pressure = bmp.seaLevelForAltitude(altitude, newPressure);

  // --- RF (RCSwitch) ---
  if (mySwitch.available()) {
    long receivedValue = mySwitch.getReceivedValue();
    mySwitch.resetAvailable();

    if (receivedValue != 0) {
      // Zakładamy, że nadajnik wysyła temp * 100
      // np. 2437 -> 24.37 C
      g_tempDS = (float)receivedValue / 100.0;
      g_firstPacketReceived = true;
      g_lastRFMillis = millis(); // nowy pakiet = aktualizacja czasu
      Serial.print("RF: Otrzymano wartosc = ");
      Serial.println(receivedValue);
      Serial.print("Przeliczona temp zew = ");
      Serial.println(g_tempDS);
    } else {
      Serial.println("RF: Odebrano kod 0 (niezidentyfikowany).");
    }
  }
}

/***********************************************************
   Funkcja drawStaticLabels()
   - Rysuje stałe etykiety na TFT (wywołanie raz w setup)
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
   - Czyści fragmenty ekranu i wyświetla bieżące wartości
   - Sprawdza, czy minęło 60s od ostatniego pakietu RF
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

  // AHT20: g_tempAHT, g_humidity
  tft.setCursor(valueX, yTemp);
  tft.printf("%.2f ÷C  ", g_tempAHT);

  tft.setCursor(230, yHum);
  tft.printf("%.1f %%", g_humidity);

  // BMP280: g_pressure
  tft.setCursor(valueX, yPres);
  tft.printf("%.1f hPa", g_pressure);

  // RF: g_tempDS lub "Brak polaczenia"
  tft.setCursor(valueX, yTempZew);
  unsigned long now = millis();
  if (!g_firstPacketReceived) {
    // Jeszcze nigdy nie dostaliśmy pakietu -> Brak połączenia
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.print("Brak polaczenia");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
  } else {
    // Mieliśmy już co najmniej jeden pakiet
    if ((now - g_lastRFMillis) > RF_TIMEOUT_MS) {
      // Minęło >60s od ostatniego pakietu -> znowu Brak połączenia
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.print("Brak polaczenia");
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
    } else {
      // Mniej niż 60s -> wyświetlamy g_tempDS
      tft.printf("%.2f ÷C               ", g_tempDS); 
    }
  }
  drawWifiStatus();
  Serial.println("Dynamic data zaktualizowane");
}

/***********************************************************
   Funkcja updateTFT()
   - Najpierw readSensors(), potem updateDynamicData()
************************************************************/
void updateTFT() {
  readSensors();
  updateDynamicData();
}

/***********************************************************
   Funkcja sendDataBlynk()
   - readSensors() -> wysyła do Blynk
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
   FUNKCJE LACZENIA Z WIFI/BLYNK
   - przykład nieblokującej próby WiFi
************************************************************/
void connectWifiShort() {
  Serial.println("Proba szybkiego polaczenia z WiFi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin("FunBox2-BE54", "654321asg");

  unsigned long startAttempt = millis();
  while ((millis() - startAttempt) < 5000) { // 5 sek. limit
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      Serial.println("WiFi polaczone!");
      return;
    }
    delay(100);
  }
  Serial.println("Nie udalo sie polaczyc z WiFi w 5 sek.");
}

void connectBlynkShort() {
  if (!wifiConnected) {
    Serial.println("Brak WiFi - nie lacze Blynk");
    return;
  }
  Serial.println("Proba polaczenia z Blynk (5 sek. timeout)...");
  Blynk.config(BLYNK_AUTH_TOKEN);
  unsigned long startAttempt = millis();
  while ((millis() - startAttempt) < 5000) {
    Blynk.run();
    if (Blynk.connected()) {
      blynkConnected = true;
      Serial.println("Blynk polaczony!");
      return;
    }
    delay(100);
  }
  Serial.println("Nie udalo sie polaczyc z Blynk w 5 sek.");
}

/***********************************************************
   Funkcja checkWiFiStatus():
   - Wywoływana co 10 sek. przez timer
   - Jeśli WiFi rozłączone -> krótka próba connectWifiShort()
   - Jeśli się uda -> connectBlynkShort()
   - Zawsze na końcu updateDynamicData() (by zaktualizować napis WiFi)
************************************************************/
void checkWiFiStatus() {
  if (WiFi.status() == WL_CONNECTED) {
    // WiFi jest ok
    wifiConnected = true;
  } else {
    wifiConnected = false;
    // Krótka próba połączenia
    connectWifiShort();
    // Jeśli się uda, łączymy Blynk
    if (wifiConnected) {
      connectBlynkShort();
    }
  }
  // Odśwież ekran, by zaktualizować napis WiFi
  updateDynamicData();
}


/***********************************************************
   Funkcja setup()
   - Najpierw czujniki + TFT
   - Odczyt + wyswietlenie
   - Potem krotka proba WiFi + Blynk
   - Timery
************************************************************/
void setup() {
  Serial.begin(115200);
  delay(1000);

  // --- Inicjalizacja I2C ---
  Wire.setPins(SDA_PIN, SCL_PIN);
  Wire.begin();

  // --- Inicjalizacja AHT20 ---
  if (!aht.begin()) {
    Serial.println("Nie udalo sie zainicjowac AHT20!");
    // ewentualnie: while(1);
  }

  // --- Inicjalizacja BMP280 ---
  if (!bmp.begin(0x76)) {
    Serial.println("BMP280 nie znaleziony na 0x76, proba 0x77...");
    if (!bmp.begin(0x77)) {
      Serial.println("BMP280 nie znaleziony. STOP");
      // while(1);
    }
  }
  bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                  Adafruit_BMP280::SAMPLING_X1,
                  Adafruit_BMP280::SAMPLING_X1,
                  Adafruit_BMP280::FILTER_OFF,
                  Adafruit_BMP280::STANDBY_MS_1000);

  // --- Inicjalizacja ekranu TFT ---
  tft.init();
  tft.setRotation(1);
  drawStaticLabels(); 

  // --- Inicjalizacja RCSwitch (RF) ---
  mySwitch.enableReceive(RX_PIN);
  mySwitch.setProtocol(1);
  mySwitch.setPulseLength(350);
  Serial.println("Odbiornik RF na pinie: " + String(RX_PIN));

  // 1) Najpierw odczyt czujnikow i wyswietlenie (by nie bylo pustego ekranu)
  updateTFT(); // readSensors + updateDynamicData

  // 2) Krótka próba WiFi
  connectWifiShort();
  // 3) Krótka próba Blynk
  if (wifiConnected) {
    connectBlynkShort();
  }


  // 4) Ustawienie timerów
  timer.setInterval(3000L, updateTFT);      // co 3 sekundy odczyt + TFT
  timer.setInterval(300000L, sendDataBlynk);// co 5 min Blynk
  timer.setInterval(10000L, checkWiFiStatus); // co 10 sek sprawdzamy WiFi

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
