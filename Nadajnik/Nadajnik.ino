#include <RCSwitch.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "driver/gpio.h"

/* =======================
   KONFIGURACJA PINÓW
   ======================= */
#define LED_PIN        2
#define TX_DATA_PIN    4      // DATA RF 433 MHz
#define TX_ENABLE_PIN  16     // ENABLE RF 
#define ONE_WIRE_BUS   17      // DS18B20

/* =======================
   KONFIGURACJA SYSTEMU
   ======================= */
#define DEBUG_MODE false              
#define SLEEP_TIME_SECONDS 30         

/* =======================
   OBIEKTY
   ======================= */
RCSwitch mySwitch;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

/* =======================
   SETUP
   ======================= */
void setup() {
  Serial.begin(115200);
  pinMode(2, OUTPUT);
  digitalWrite(2, HIGH);   // HIGH = LED ZGASZONA (bo LED do VCC)
  gpio_hold_en(GPIO_NUM_2);
  delay(1500);

  Serial.printf("reset_reason=%d, wakeup_cause=%d\n",
              (int)esp_reset_reason(),
              (int)esp_sleep_get_wakeup_cause());
  Serial.println("\n\n--- START ESP32 RF TEMP ---");

  /* LED */
  // pinMode(LED_PIN, OUTPUT);
  // digitalWrite(LED_PIN, LOW);

  /* ENABLE RF – NA START WYŁĄCZONY */
  pinMode(TX_ENABLE_PIN, OUTPUT);
  digitalWrite(TX_ENABLE_PIN, LOW);

  /* DAJ ESP CZAS NA STABILIZACJĘ */
  delay(2000);

  /* INICJALIZACJA DS18B20 */
  Serial.println("Init DS18B20...");
  sensors.begin();
  delay(500);

  int deviceCount = sensors.getDeviceCount();
  Serial.print("DS18B20 count: ");
  Serial.println(deviceCount);

  if (deviceCount == 0) {
    Serial.println("BŁĄD: Brak czujnika DS18B20!");
    // blinkLED(5);
  }

  /* WŁĄCZENIE RF PO STABILIZACJI */
  Serial.println("Włączam RF...");
  digitalWrite(TX_ENABLE_PIN, HIGH);
  delay(200);

  mySwitch.enableTransmit(TX_DATA_PIN);
  mySwitch.setProtocol(1);
  mySwitch.setPulseLength(350);

  Serial.println("RF gotowy");

  /* JEDEN CYKL */
  sendTemperature();

  if (DEBUG_MODE) {
    Serial.println("TRYB DEBUG – brak deep sleep");
  } else {
    goToSleep();
  }
}

/* =======================
   LOOP
   ======================= */
void loop() {
  if (DEBUG_MODE) {
    delay(5000);
    sendTemperature();
  }
}

/* =======================
   FUNKCJE
   ======================= */
void sendTemperature() {
  Serial.println("Pobieram temperaturę...");

  sensors.requestTemperatures();
  delay(750);

  float tempC = sensors.getTempCByIndex(0);

  if (tempC == DEVICE_DISCONNECTED_C || tempC == 85.0) {
    Serial.println("BŁĄD ODCZYTU DS18B20");
    // blinkLED(3);
    return;
  }

  long tempToSend = (long)(tempC * 100);

  Serial.print("Temp: ");
  Serial.print(tempC);
  Serial.print(" °C  |  TX: ");
  Serial.println(tempToSend);

  /* TRANSMISJA */
  for (int i = 0; i < 10; i++) {
    mySwitch.send(tempToSend, 24);
    delay(200);
  }

  Serial.println("TX DONE");
}

/* =======================
   DEEP SLEEP
   ======================= */
void goToSleep() {
  Serial.println("Wyłączam RF i idę spać...");
  digitalWrite(TX_ENABLE_PIN, LOW);
  Serial.flush();

  esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_TIME_SECONDS * 1000000ULL);
  gpio_hold_en(GPIO_NUM_2);
  esp_deep_sleep_start();
}

/* =======================
   LED ERROR
   ======================= */
// void blinkLED(int times) {
//   for (int i = 0; i < times; i++) {
//     digitalWrite(LED_PIN, HIGH);
//     delay(200);
//     digitalWrite(LED_PIN, LOW);
//     delay(200);
//   }
// }
