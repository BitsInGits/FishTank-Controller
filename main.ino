#include <Arduino.h>
#include <WiFi.h>
#include <pgmspace.h>
#include <time.h>
#include <array>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <RCSwitch.h>
#include <Adafruit_NeoPixel.h>
#include <TFT_eSPI.h>
#include <SPI.h>

// Define global variables
unsigned long lastInput          = 0; // last time button was pressed (on esp32)
unsigned long lastLightOverwrite = 0; // when was the last manual overwrite
unsigned long lastCoolOverwrite  = 0; // when was the last manual overwrite
unsigned long lastPumpOverwrite  = 0; // when was the last manual overwrite

bool pump  = 0; // is pump on
bool cool  = 0; // is cool on
bool light = 0; // is light on

const char buttonL =  0; // LeftButton Pin on ESP
const char buttonR = 35; // RightButton Pin on ESP
const char hbridge =  2; // H-Bridge Pin on ESP

short temperature                =  18; // current average temperature
short sunrise                    =   7; // when does the sunrise start (brighten 1h long)
short sunset                     =  20; // from when on should the sun set (1h long till off)
short pumpStart                  =   0; // start time for the air pump
short pumpStop                   =   6; // end time for the air pump
short lightOverwriteDuration     =   1; // how long are manual overwrites used for the light (in h)
short pumpOverwriteDuration      =   8; // how long are manual overwrites used for the pump (in h)
short coolOverwriteDuration      =   8; // how long are manual overwrites used for the fan (in h)
short temperatureArray[90];     // Array for all Temperature Values
short startTemperature   =  18; // Temp thats used to fill the array at initalisation
short tempCounter        =   0; // Counts the accessed elements
short buttonBuffer       = 200; // delay at the start of every display function, so that you dont make many actions with one click

// Mutexes
SemaphoreHandle_t lightMutex = xSemaphoreCreateMutex();
SemaphoreHandle_t   airMutex = xSemaphoreCreateMutex();
SemaphoreHandle_t   fanMutex = xSemaphoreCreateMutex();
SemaphoreHandle_t      Mutex = xSemaphoreCreateMutex();

//Create Objects
WiFiServer server(80); //Webserver Object with Port
OneWire oneWire_in(27); // OneWire Object for the Temperature Sensor
DallasTemperature TemperatureSensor(&oneWire_in); // Temperature Sensor Object
TFT_eSPI tft = TFT_eSPI(135, 240);  // Invoke library, pins defined in User_Setup.h
RCSwitch rcSwitch = RCSwitch(); // RC switch object
struct tm timeinfo; // Object for saving timeinfos
Adafruit_NeoPixel strip(330, 17, NEO_GRBW + NEO_KHZ800); // Declare our NeoPixel strip object, (LED_COUNT, LED_PIN, ...)

//NUM OF LEDS SET TO 330 CAUSE OF ISSUES; RESET TO 230

extern "C" {
  uint8_t temprature_sens_read(); // used to read the Termperature sensor
}

///////////////////////////////////FUNCTIONS//////////////////////////////////
void displayOn() {
  // Serial.println("on Function");
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
}


void displayOff() {
  lastInput = millis() - 100000;
  tft.writecommand(ST7789_DISPOFF);
  digitalWrite(TFT_BL, LOW);// Turn off BL
}


void screen0() {
  tft.fillScreen(TFT_BLACK);

  while (millis() - lastInput <= 9999) {
    tft.setCursor(0, 0, 2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1); tft.println(":)");
    tft.setTextSize(1); tft.print("CPU: "); tft.print((temprature_sens_read() - 32) / 1.8); tft.println(" C");
    tft.setTextSize(1); tft.print("IP: "); tft.print(WiFi.localIP());

    vTaskDelay(pdMS_TO_TICKS(buttonBuffer));

    if (digitalRead(buttonL) == 0) {
      lastInput = millis();
      screen1();
    }
    if (digitalRead(buttonR) == 0) {
      lastInput = millis();
      tft.fillScreen(TFT_BLACK);
      tft.setCursor(0, 0, 2);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      while (digitalRead(buttonR) == 0 && millis() - lastInput <= 9999) {
        tft.setTextSize(1); tft.println(":o");
      }
      vTaskDelay(pdMS_TO_TICKS(buttonBuffer / 2));
    }
  }
}


void screen1() {
  tft.fillScreen(TFT_BLACK);

  while (millis() - lastInput <= 9999) {
    getLocalTime(&timeinfo);

    tft.setCursor(0, 0, 2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2); tft.println(&timeinfo, "              %H:%M");

    tft.setCursor(0, 35, 2);
    tft.setTextSize(7); tft.print(temperature); tft.print(" C");

    vTaskDelay(pdMS_TO_TICKS(buttonBuffer));

    if (digitalRead(buttonL) == 0) {
      lastInput = millis();
      screen2();
    }
    if (digitalRead(buttonR) == 0) {
      lastInput = millis();
      screen0();
    }
  }
}


void screen2() {
  unsigned long pressStart = 0; // when you startet pressing the button, to register long presses
  unsigned long pressEnd   = 0; // when you stopped pressing the button

  tft.fillScreen(TFT_BLACK);

  while (millis() - lastInput <= 9999) {
    tft.setCursor(0, 0, 2); // x, y, font


    if (lightOverwriteDuration * 3600000 > millis() - lastLightOverwrite) {
      tft.setTextColor(TFT_RED, TFT_BLACK); tft.setTextSize(2); tft.println("MANUELL");
    } else {
      tft.setTextColor(TFT_GREEN, TFT_BLACK); tft.setTextSize(2); tft.println("AUTOMATISCH");
    }

    tft.setCursor(0, 80, 1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    if (light == 1) {
      tft.setTextSize(7); tft.println("LED:I");
    } else {
      tft.setTextSize(7); tft.println("LED:O");
    }

    vTaskDelay(pdMS_TO_TICKS(buttonBuffer));

    if (digitalRead(buttonL) == 0) {
      lastInput = millis();
      screen3();
    }
    if (digitalRead(buttonR) == 0) {
      lastInput  = millis();
      pressStart = millis();

      while (digitalRead(buttonR) == 0 && millis() - lastInput <= 9999 && millis() - pressStart < 1100) {
        //pass
      }

      pressEnd = millis();

      // Serial.println(pressEnd-pressStart);

      if (pressEnd - pressStart < 1000) { // short press
        // Serial.println("Short");
        if (light == 0) {
          lightOn();
          lastLightOverwrite = millis();
          tft.fillScreen(TFT_BLACK); ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        } else {
          lightOff();
          lastLightOverwrite = millis();
          tft.fillScreen(TFT_BLACK); ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        }
      }
      if (pressEnd - pressStart > 1000) { // long press
        if (lightOverwriteDuration * 3600000 < millis()) {
          // Serial.println("Long");
          if (lightOverwriteDuration * 3600000 > millis() - lastLightOverwrite) {
            // Serial.println("is manual, set to auto");
            // is manual, set to automatic
            lastLightOverwrite = 0;
            tft.fillScreen(TFT_BLACK);
            tft.setCursor(0, 0, 2); // x, y, font
            tft.setTextColor(TFT_GREEN, TFT_BLACK); tft.setTextSize(2); tft.println("AUTOMATISCH");
            vTaskDelay(pdMS_TO_TICKS(1000));
          } else {
            // is automatic, set to manual for 12h
            lastLightOverwrite = millis();
            tft.fillScreen(TFT_BLACK);
            tft.setCursor(0, 0, 2); // x, y, font
            tft.setTextColor(TFT_RED, TFT_BLACK); tft.setTextSize(2); tft.println("MANUELL");
            vTaskDelay(pdMS_TO_TICKS(1000));
          }
        } else {
          tft.fillScreen(TFT_BLACK);
          tft.setCursor(0, 0, 2); // x, y, font
          tft.setTextColor(TFT_BLUE, TFT_BLACK); tft.setTextSize(2); tft.print("Automatischer    Modus erst nach  "); tft.print(lightOverwriteDuration); tft.print("h verfuegbar");
          vTaskDelay(pdMS_TO_TICKS(5000));
          tft.fillScreen(TFT_BLACK);
        }
      }
    }
  }
}


void screen3() {
  unsigned long pressStart = 0; // when you startet pressing the button, to register long presses
  unsigned long pressEnd   = 0; // when you stopped pressing the button

  tft.fillScreen(TFT_BLACK);

  while (millis() - lastInput <= 9999) {
    tft.setCursor(0, 0, 2); // x, y, font

    if (pumpOverwriteDuration * 3600000 > millis() - lastPumpOverwrite) {
      tft.setTextColor(TFT_RED, TFT_BLACK); tft.setTextSize(2); tft.println("MANUELL");
    } else {
      tft.setTextColor(TFT_GREEN, TFT_BLACK); tft.setTextSize(2); tft.println("AUTOMATISCH");
    }

    tft.setCursor(0, 80, 1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    if (pump == 1) {
      tft.setTextSize(7); tft.println("Air:I");
    } else {
      tft.setTextSize(7); tft.println("Air:O");
    }

    vTaskDelay(pdMS_TO_TICKS(buttonBuffer));

    if (digitalRead(buttonL) == 0) {
      lastInput = millis();
      screen4();
    }
    if (digitalRead(buttonR) == 0) {
      lastInput  = millis();
      pressStart = millis();

      while (digitalRead(buttonR) == 0 && millis() - lastInput <= 9999 && millis() - pressStart < 1100) {
        // wait
      }

      pressEnd = millis();

      if (pressEnd - pressStart < 1000) { // short press
        if (pump == 0) {
          pumpOn();
          lastPumpOverwrite = millis();
          tft.fillScreen(TFT_BLACK); ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        } else {
          pumpOff();
          lastPumpOverwrite = millis();
          tft.fillScreen(TFT_BLACK); ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        }
      }
      if (pressEnd - pressStart > 1000) { // long press
        if (pumpOverwriteDuration * 3600000 < millis()) {
          if (pumpOverwriteDuration * 3600000 > millis() - lastPumpOverwrite) {
            // is manual, set to automatic
            lastPumpOverwrite = 0;
            tft.fillScreen(TFT_BLACK);
            tft.setCursor(0, 0, 2); // x, y, font
            tft.setTextColor(TFT_GREEN, TFT_BLACK); tft.setTextSize(2); tft.println("AUTOMATISCH");
            vTaskDelay(pdMS_TO_TICKS(1000));
          } else {
            // is automatic, set to manual for 1h
            lastPumpOverwrite = millis();
            tft.fillScreen(TFT_BLACK);
            tft.setCursor(0, 0, 2); // x, y, font
            tft.setTextColor(TFT_RED, TFT_BLACK); tft.setTextSize(2); tft.println("MANUELL");
            vTaskDelay(pdMS_TO_TICKS(1000));
          }
        } else {
          tft.fillScreen(TFT_BLACK);
          tft.setCursor(0, 0, 2); // x, y, font
          tft.setTextColor(TFT_BLUE, TFT_BLACK); tft.setTextSize(2); tft.print("Automatischer    Modus erst nach  "); tft.print(pumpOverwriteDuration); tft.print("h verfuegbar");
          vTaskDelay(pdMS_TO_TICKS(5000));
          tft.fillScreen(TFT_BLACK);
        }
      }
    }
  }
}


void screen4() {
  unsigned long pressStart = 0; // when you startet pressing the button, to register long presses
  unsigned long pressEnd   = 0; // when you stopped pressing the button

  tft.fillScreen(TFT_BLACK);

  while (millis() - lastInput <= 9999) {
    tft.setCursor(0, 0, 2); // x, y, font

    if (coolOverwriteDuration * 3600000 > millis() - lastCoolOverwrite) {
      tft.setTextColor(TFT_RED, TFT_BLACK); tft.setTextSize(2); tft.println("MANUELL");
    } else {
      tft.setTextColor(TFT_GREEN, TFT_BLACK); tft.setTextSize(2); tft.println("AUTOMATISCH");
    }

    tft.setCursor(0, 80, 1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    if (cool == 1) {
      tft.setTextSize(7); tft.println("Fan:I");
    } else {
      tft.setTextSize(7); tft.println("Fan:O");
    }

    vTaskDelay(pdMS_TO_TICKS(buttonBuffer));

    if (digitalRead(buttonL) == 0) {
      lastInput = millis();
      screen5();
    }
    if (digitalRead(buttonR) == 0) {
      lastInput  = millis();
      pressStart = millis();

      while (digitalRead(buttonR) == 0 && millis() - lastInput <= 9999 && millis() - pressStart < 1100) {
        // wait
      }

      pressEnd = millis();

      if (pressEnd - pressStart < 1000) { // short press
        if (cool == 0) {
          coolOn();
          lastCoolOverwrite = millis();
          tft.fillScreen(TFT_BLACK); ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        } else {
          coolOff();
          lastCoolOverwrite = millis();
          tft.fillScreen(TFT_BLACK); ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        }
      }
      if (pressEnd - pressStart > 1000) { // long press
        if (coolOverwriteDuration * 3600000 < millis()) {
          if (coolOverwriteDuration * 3600000 > millis() - lastCoolOverwrite) {
            // is manual, set to automatic
            lastCoolOverwrite = 0;
            tft.fillScreen(TFT_BLACK);
            tft.setCursor(0, 0, 2); // x, y, font
            tft.setTextColor(TFT_GREEN, TFT_BLACK); tft.setTextSize(2); tft.println("AUTOMATISCH");
            vTaskDelay(pdMS_TO_TICKS(1000));
          } else {
            // is automatic, set to manual for 1h
            lastCoolOverwrite = millis();
            tft.fillScreen(TFT_BLACK);
            tft.setCursor(0, 0, 2); // x, y, font
            tft.setTextColor(TFT_RED, TFT_BLACK); tft.setTextSize(2); tft.println("MANUELL");
            vTaskDelay(pdMS_TO_TICKS(1000));
          }
        } else {
          tft.fillScreen(TFT_BLACK);
          tft.setCursor(0, 0, 2); // x, y, font
          tft.setTextColor(TFT_BLUE, TFT_BLACK); tft.setTextSize(2); tft.print("Automatischer    Modus erst nach  "); tft.print(coolOverwriteDuration); tft.print("h verfuegbar");
          vTaskDelay(pdMS_TO_TICKS(5000));
          tft.fillScreen(TFT_BLACK);
        }
      }
    }
  }
}


int screen5Update(short R, short G, short B, short W, short selected) {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0, 2); // x, y, font
  if (selected == 1) {
    tft.setTextColor(TFT_BLUE, TFT_BLACK);
  }
  else {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
  }
  tft.setTextSize(3); tft.print("R: "); tft.print(R);
  tft.setCursor(0, 40, 2); // x, y, font
  if (selected == 2) {
    tft.setTextColor(TFT_BLUE, TFT_BLACK);
  }
  else {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
  }
  tft.setTextSize(3); tft.print("G: "); tft.print(G);
  tft.setCursor(0, 80, 2); // x, y, font
  if (selected == 3) {
    tft.setTextColor(TFT_BLUE, TFT_BLACK);
  }
  else {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
  }
  tft.setTextSize(3); tft.print("B: "); tft.print(B);
  tft.setCursor(140, 20, 2); // x, y, font
  if (selected == 4) {
    tft.setTextColor(TFT_BLUE, TFT_BLACK);
  }
  else {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
  }
  tft.setTextSize(3); tft.print("Hell:");
  tft.setCursor(140, 60, 2); // x, y, font
  tft.setTextSize(3); tft.print(W);
  return 0;
}


void screen5() {
  unsigned long pressStart = 0; // when you startet pressing the button, to register long presses
  unsigned long pressEnd   = 0; // when you stopped pressing the button

  short R        = 250;
  short G        =   0;
  short B        =  50;
  short W        = 165;
  short selected =   0;

  screen5Update(R, G, B, W, selected);

  while (millis() - lastInput <= 9999) {
    vTaskDelay(pdMS_TO_TICKS(buttonBuffer));

    if (digitalRead(buttonL) == 0 && selected == 0) {
      lastInput = millis();
      screen1();
    }

    if (digitalRead(buttonR) == 0) {
      lastInput = millis();
      selected += 1;
      if (selected > 4) {
        selected = 0;
      }
      screen5Update(R, G, B, W, selected);
    }

    if (digitalRead(buttonL) == 0 && selected > 0) {
      lastInput = millis();
      if (selected == 1) {
        R += 5;
      }
      if (selected == 2) {
        G += 5;
      }
      if (selected == 3) {
        B += 5;
      }
      if (selected == 4) {
        W += 5;
      }
      if (R > 255) {
        R = 0;
      }
      if (G > 255) {
        G = 0;
      }
      if (B > 255) {
        B = 0;
      }
      if (W > 165) {
        W = 0;
      }
      screen5Update(R, G, B, W, selected);
      strip.setBrightness(W);
      colorWipe(strip.Color(  R,   G,   B));
      lastLightOverwrite = millis();
      light = 1;
    }
  }
}


void colorWipe(uint32_t color) {
  for (int i = 0; i < strip.numPixels(); i++) { // For each pixel in strip...
    strip.setPixelColor(i, color);         // Set pixel's color (in RAM)
  }
  strip.show();                            // Update strip to match
}


void getTemperature() {
  short  temp = 18;
  int tempSum =  0;

  TemperatureSensor.requestTemperatures();
  vTaskDelay(pdMS_TO_TICKS(10));
  temp = TemperatureSensor.getTempCByIndex(0);

  if (temp >= 2 && temp <= 30) {
    temperatureArray[tempCounter] = temp;
    tempCounter += 1;

    if (tempCounter == 90) {
      tempCounter = 0;
    }

    for (int i = 0; i < 90 ; i++) {
      tempSum += temperatureArray[i];
    }

    temperature = tempSum / 90;

    if (temperature >= 20 && coolOverwriteDuration * 3600000 < millis() - lastCoolOverwrite) {
      coolOn();
    } else if (coolOverwriteDuration * 3600000 < millis() - lastCoolOverwrite) {
      coolOff();
    }
  }
}


void coolOn() {
  digitalWrite(hbridge, HIGH);
  cool = 1;
}


void coolOff() {
  digitalWrite(hbridge, LOW);
  cool = 0;
}


void pumpOn() {
  rcSwitch.switchOn("11111", "00010"); // Luft
  pump = 1;
}


void pumpOff() {
  rcSwitch.switchOff("11111", "00010"); // Luft
  pump = 0;
}


void lightOn() {
  strip.setBrightness(165);
  colorWipe(strip.Color( 0, 0, 0, 255)); // day
  light = 1;
}


void lightOff() {
  colorWipe(strip.Color( 0, 0, 0, 0));   // night
  light = 0;
}



void Server(void *pvParameters) { // Task function for Core 0
  // Serial.println("Start Server-Setup");
  ///////////////////////////////////VARIABLES//////////////////////////////////
  String redString = "0";
  String greenString = "0";
  String blueString = "0";

  int brightnessValue = 0;
  int fanSpeedValue = 0;
  // Serial.print("Remaining stack start of Server loop:"); // Serial.println(uxTaskGetStackHighWaterMark(NULL));
  const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
  <head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>ESP WebControl</title>
    <script src="https://code.jquery.com/jquery-3.6.0.min.js"></script>
    <style>
        body {
            font-family: Arial, sans-serif;
            text-align: center;
            background-color: #f4f4f4;
            color: #333;
            margin: 0;
            padding: 20px;
        }
        h1 {
            color: #4CAF50;
        }
        .button {
            background-color: #008CBA;
            color: white;
            padding: 14px 20px;
            margin: 10px;
            border: none;
            cursor: pointer;
            font-size: 16px;
            transition: background-color 0.3s;
            border-radius: 5px;
        }
        .button:hover {
            background-color: #005f6b;
        }
        .slider {
            -webkit-appearance: none;
            width: 100%;
            height: 15px;
            background: #ddd;
            outline: none;
            opacity: 0.7;
            transition: opacity .2s;
            margin: 10px 0;
        }
        .slider:hover {
            opacity: 1;
        }
        #SliderValue {
            font-weight: bold;
        }
        #colorPicker {
            margin: 20px;
            display: inline-block;
        }
        .control-group {
            margin: 20px 0;
            padding: 10px;
            border: 1px solid #ccc;
            border-radius: 5px;
            background-color: #fff;
        }
    </style>
  </head>
  <body>
    <h1>ESP Server</h1>

    <!-- LED Control Group -->
    <div class="control-group">
        <!-- LED ON/OFF -->
        <p>LED - %LED_STATE%</p>
        <p><a href="/led/toggle"><button class="button">%LED_TEXT%</button></a></p>

        <!-- Brightness Slider -->
        <p>Helligkeit: <span id="SliderValue">%SLIDERVALUE%</span></p>
        <input type="range" id="brightness" min="0" max="120" value="%SLIDERVALUE%" step="1" oninput="updateBrightness(this)">

        <!-- Mobile-friendly Color Picker -->
        <h2>LED-Farbe</h2>
        <input type="color" id="colorPicker" onchange="updateColor(this.value)" value="#ffffff"/> <!-- Default to white -->
    </div>

    <!-- Air Control Group -->
    <div class="control-group">
        <!-- Air ON/OFF -->
        <p>Luft - %AIR_STATE%</p>
        <p><a href="/air/toggle"><button class="button">%AIR_TEXT%</button></a></p>
    </div>

    <!-- Fan Control Group -->
    <div class="control-group">
        <!-- FAN ON/OFF -->
        <p>Ventilator - %FAN_STATE%</p>
        <p><a href="/fan/toggle"><button class="button">%FAN_TEXT%</button></a></p>
    </div>

    <!-- Values -->
    <div class="control-group">
        <p>Temperatur: %TEMPERATUR%</p>
    </div>

    <script>
        function updateColor(hex) {
            // If white is selected, treat it as "no color"
            if (hex === "#ffffff") {
                // Optionally, you can reset the RGB values to zero or handle it differently
                window.location.href = `/?r=0&g=0&b=0`; // Reset to "off" or another color
            } else {
                // Extract RGB from hex value
                var r = parseInt(hex.slice(1, 3), 16);
                var g = parseInt(hex.slice(3, 5), 16);
                var b = parseInt(hex.slice(5, 7), 16);
                // Redirect to the server with RGB values
                window.location.href = `/?r=${r}&g=${g}&b=${b}`;
            }
        }

        function resetColor() {
            // Reset color to white
            document.getElementById('colorPicker').value = '#ffffff';
            updateColor('#ffffff'); // Treat as "no color"
        }

        function updateBrightness(slider) {
            var sliderValue = slider.value;
            document.getElementById("SliderValue").innerText = sliderValue;
            // Send an AJAX request to update brightness
            $.get(`/?brightness=${sliderValue}`);
        }
    </script>
  </body>
</html>
)rawliteral";


  ///////////////////////////////////SETUP//////////////////////////////////


  ///////////////////////////////////LOOP//////////////////////////////////
  // Serial.println("Start Server-Loop");
  while (true) {
    // Serial.println("Server Loop durchlauf");
    // Serial.print("Remaining stack at 1:"); // Serial.println(uxTaskGetStackHighWaterMark(NULL));
    WiFiClient client = server.available();  // Listen for incoming clients
    if (client){
      // Serial.print("Client found, Stack:"); // Serial.println(uxTaskGetStackHighWaterMark(NULL));

      // Read incoming request
      String request = client.readStringUntil('\r');
      client.flush();
    
      // Toggle LED (light) state
      if (request.indexOf("/led/toggle") >= 0) {
        if (xSemaphoreTake(lightMutex, portMAX_DELAY) == pdTRUE) { // Lock the mutex for light
          light = !light;  // Toggle light state
          xSemaphoreGive(lightMutex); // Release the mutex
        }

        // Set the state of the light
        if (light) {
          lightOn();
          lastLightOverwrite = millis();
        } else {
          lightOff();
          lastLightOverwrite = millis();
        }
      }

      // Handle air toggle requests
      if (request.indexOf("/air/toggle") >= 0) {
        if (xSemaphoreTake(airMutex, portMAX_DELAY) == pdTRUE) { // Lock the mutex for air
          pump = !pump;  // Toggle air state
          xSemaphoreGive(airMutex); // Release the mutex
        }

        // Set the state of the air
        if (pump) {
          pumpOn();
          lastPumpOverwrite = millis();
        } else {
          pumpOff();
          lastPumpOverwrite = millis();
        }
      }

      // Handle fan toggle requests
      if (request.indexOf("/fan/toggle") >= 0) {
        if (xSemaphoreTake(fanMutex, portMAX_DELAY) == pdTRUE) { // Lock the mutex for fan
          cool = !cool;  // Toggle fan state
          xSemaphoreGive(fanMutex); // Release the mutex
        }

        // Set the state of the fan
        if (cool) {
          coolOn();
          lastCoolOverwrite = millis();
        } else {
          coolOff();
          lastCoolOverwrite = millis();
        }
      }

      // Handle brightness requests
      if (request.indexOf("/?brightness=") >= 0) {
        int pos1 = request.indexOf('=');
        brightnessValue = request.substring(pos1 + 1).toInt();
        strip.setBrightness(brightnessValue);
        strip.show();
        lastLightOverwrite = millis();
      }

      // Handle RGB color requests
        if (request.indexOf("/?r=") >= 0) {
          int rPos = request.indexOf("r=") + 2;  // Start of red value
          int gPos = request.indexOf("g=") + 2;  // Start of green value
          int bPos = request.indexOf("b=") + 2;  // Start of blue value

          // Use the next parameter position, or the end of the string, for blue
          int endPos = request.indexOf('&', bPos);
          if (endPos == -1) {
            endPos = request.length();
          }

          // Extract each color as a substring
          redString = request.substring(rPos, gPos - 3);       // red value
          greenString = request.substring(gPos, bPos - 3);     // green value
          blueString = request.substring(bPos, endPos);        // blue value

          // Convert string to integer and set color
          strip.setBrightness(120);
          colorWipe(strip.Color(redString.toInt(), greenString.toInt(), blueString.toInt()));
          lastLightOverwrite = millis();

          if (xSemaphoreTake(lightMutex, portMAX_DELAY) == pdTRUE) { // Lock the mutex for light
            light = 1;  // Toggle light state
            xSemaphoreGive(lightMutex); // Release the mutex
          }

          Serial.println(redString);
        }

      // Send HTML response
      String response = FPSTR(index_html);
      response.replace("%LED_STATE%", (light == 1) ? "AN" : "AUS");
      response.replace("%LED_TEXT%",  (light == 1) ? "LED AUS" : "LED AN");
    
      response.replace("%AIR_STATE%", (pump == 1) ? "AN" : "AUS");
      response.replace("%AIR_TEXT%",  (pump == 1) ? "Luft AUS" : "Luft AN");
    
      response.replace("%FAN_STATE%", (cool == 1) ? "AN" : "AUS");
      response.replace("%FAN_TEXT%",  (cool == 1) ? "Ventilator AUS" : "Ventilator AN");

      response.replace("%SLIDERVALUE%", String(brightnessValue));
      response.replace("%TEMPERATUR%", String(temperature));

      client.println("HTTP/1.1 200 OK");
      client.println("Content-type:text/html");
      client.println("Connection: close");
      client.println();
      client.println(response);
      client.stop();

      // if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) { // Lock the mutex before modifying the shared variable, if checks if lock was succesful
      //   sharedVariable++; // Modify shared variable
      //   xSemaphoreGive(mutex); // Release the mutex
    }
  vTaskDelay(pdMS_TO_TICKS(10));
  }
}



void Hardware(void *pvParameters) { // Task function for Core 1
// Serial.println("Start Hardware-Setup");
  ///////////////////////////////////SETUP//////////////////////////////////
  for (int i = 0; i < 90; i++) {
    temperatureArray[i] = startTemperature;
  }

  pinMode(hbridge, OUTPUT);
  pinMode(buttonL, INPUT_PULLUP);
  pinMode(buttonR, INPUT_PULLUP);


  ///////////////////////////////////LOOP//////////////////////////////////
// Serial.println("Start Hardware-Loop");
  while (true) {
  // Serial.print("Remaining stack start of loop:"); // Serial.println(uxTaskGetStackHighWaterMark(NULL));
  // Serial.println("0");
    getLocalTime(&timeinfo);
  // Serial.println("1");
    if(timeinfo.tm_sec % 10 == 0){
    // Serial.println("Sensoren überprüfen, Aktoren steuern");
      getTemperature();
    }
  // Serial.println("2");
    if(timeinfo.tm_min % 10 == 0 && timeinfo.tm_sec == 45){
    // Serial.println("Zeit überprüfen, aktuelle holen");
      if(millis() > 4000000000){
      // Serial.println("Warning, Wanted Restarting");
        ESP.restart();
      }
      configTime(3600, 3600, "pool.ntp.org");
    }
  // Serial.println("3");
    if(timeinfo.tm_sec == 35){
    // Serial.println("4");
      int lightLevel = timeinfo.tm_min *4.25;

      if(lightOverwriteDuration*3600000 < millis() - lastLightOverwrite){
        strip.setBrightness(165);
      }
      
      if(timeinfo.tm_hour == sunrise && lightOverwriteDuration*3600000 < millis() - lastLightOverwrite){
        colorWipe(strip.Color(  0,   0,   0, lightLevel)); // sunrise
        light = 1;
      }
      if(timeinfo.tm_hour > sunrise && timeinfo.tm_hour < sunset && lightOverwriteDuration*3600000 < millis() - lastLightOverwrite){
        lightOn();
      }
      if(timeinfo.tm_hour == sunset && lightOverwriteDuration*3600000 < millis() - lastLightOverwrite){
        colorWipe(strip.Color(  0,   0,   0, 255-lightLevel)); // sunset
      }
      if(timeinfo.tm_hour < sunrise || timeinfo.tm_hour > sunset && lightOverwriteDuration*3600000 < millis() - lastLightOverwrite){
        lightOff();
      }

      if(timeinfo.tm_hour >= pumpStart && timeinfo.tm_hour < pumpStop && pumpOverwriteDuration*3600000 < millis() - lastPumpOverwrite){
        pumpOn();
        // Serial.println("pumpOn");
      }else if(pumpOverwriteDuration*3600000 < millis() - lastPumpOverwrite){
        pumpOff();
        // Serial.println("pumpOff");
      }
    }

    if(digitalRead(buttonL) == 0 || digitalRead(buttonR) == 0){
    // Serial.println("button press");
      lastInput = millis();
      displayOn();
      vTaskDelay(10 / portTICK_PERIOD_MS); // Wait for 1 
      screen1();
      displayOff();
    }

    vTaskDelay(10 / portTICK_PERIOD_MS); // Wait for 1 second
  }
}




void setup() {
  Serial.begin(115200);

  // Brightness of LEDs
  strip.setBrightness(165);
  
  //  Wifi+Server
  WiFi.begin("FRITZ!Box 7520 JL", "34663106453258040987"); //SSID,PSW
  delay(1000);
  server.begin();  // Start server

  // Tasks on Cores
  xTaskCreatePinnedToCore(Server, "Server",   8192, NULL, 1, NULL, 0); // Core 0 SERVER
  xTaskCreatePinnedToCore(Hardware, "Hardware", 4096, NULL, 1, NULL, 1); // Core 1 HW-Client

  // Temp Sensor
  TemperatureSensor.begin();

  // Turn off all
  pumpOff();
  lightOff();
  coolOff();

  // set Time
  configTime(3600, 3600, "pool.ntp.org");

  // rcSwitch for Air
  rcSwitch.enableTransmit(12);
  rcSwitch.setRepeatTransmit(15);
}


void loop() {
  // Empty loop as tasks run independently
}
