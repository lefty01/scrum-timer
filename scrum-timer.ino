
/*
 * Copyright (c) 2019,2020 Andreas Loeffler <al@exitzero.de>
 */

#include <NeoPixelBus.h>
#include <NeoPixelAnimator.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// TODO: optional start wifi AP & webserver for (more) config options & OTA
//       eg. light strip size or type (strip, ring, matrix)
//       store defaults in flash and add config menu to change defaults
//       possible other "apps": (tea-)timer, clock, ...
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
//#include <ESP8266WebServer.h>

const char* ssid = "SCRUMTIMER";
const char* password = "m1u2r3c4s5";

const char* VERSION = "1.1";

// rgb strip configuration
// leds on strip/ring
const uint16_t pixelCount = 60;
 // ignored for Esp8266 (always uses RX pin)
const uint8_t  pixelPin = RX;
// aka brightness (max 255)
uint8_t colorSaturation = 96; // todo: config option for brightness
RgbColor red(colorSaturation, 0, 0);
RgbColor green(0, colorSaturation, 0);
RgbColor blue(0, 0, colorSaturation);
RgbColor yellow(colorSaturation, colorSaturation, 0);
RgbColor white(colorSaturation);
RgbColor black(0);

HslColor hslRed(red);
HslColor hslGreen(green);
HslColor hslYellow(yellow);
HslColor hslWhite(white);
HslColor hslBlack(black);

// input buttons, handled via interrupt
const int button1 = D5; // menu  / reset
const int button2 = D7; // up    / increment
const int button3 = D6; // down  / decrement
const int button4 = D4; // start / next
// volatile used in isr
volatile uint8_t b1_state = LOW;
volatile uint8_t b2_state = LOW;
volatile uint8_t b3_state = LOW;
volatile uint8_t b4_state = LOW;
volatile unsigned long lastButton1 = 0;
volatile unsigned long lastButton2 = 0;
volatile unsigned long lastButton3 = 0;
volatile unsigned long lastButton4 = 0;

// button debounce time in ms
unsigned long debounceT = 300;

// mode or state where the timer is currently running in
enum modes {
  POR = 0,
  ENTER_PERSON,
  ENTER_TIME,
  SHOW_T_PER_PERSON,
  CONFIG,
  RUNNING,
  TIMEOUT
};
uint8_t mode = POR;
uint8_t timeout = 0;
bool timeoutToggle = false;

int scrumTime   = 15;   // scrum meeting time in minutes, default 15min
int persons     = 5;    // number of participants, default 5
float firstWarn = 0.1;  // warning, turn yellow if only 10% left
float finalWarn = 0.05; // final warn, if only 5% time left

float t_per_person = (float)scrumTime / (float)persons;
float timer, first_warn_t, final_warn_t;
uint16_t ledTimer;
int currentPerson = 1;

#define EVERY_SECOND 1000 // every second do...
unsigned long time_1 = 0;


// FIXME: define constant for lcd layout lines/cols
LiquidCrystal_I2C lcd(0x3F, 16, 2);
NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> strip(pixelCount, pixelPin);
NeoPixelAnimator animations(1, NEO_MILLISECONDS); // one animation for timeout


// put isr's into ram, esp might crash otherwise (due to mutlithrading)
ICACHE_RAM_ATTR void button1_cb()
{
  if ((millis() - lastButton1) > debounceT) {
    b1_state = HIGH;
    lastButton1 = millis();
  }
}
ICACHE_RAM_ATTR void button2_cb()
{
  if ((millis() - lastButton2) > debounceT) {
    b2_state = HIGH;
    lastButton2 = millis();
  }
}
ICACHE_RAM_ATTR void button3_cb()
{
  if ((millis() - lastButton3) > debounceT) {
    b3_state = HIGH;
    lastButton3 = millis();
  }
}
ICACHE_RAM_ATTR void button4_cb()
{
  if ((millis() - lastButton4) > debounceT) {
    b4_state = HIGH;
    lastButton4 = millis();
  }
}


template<typename Val>
void lcd_print_num(Val n, unsigned line=0, int offset=0)
{
  if (line > 1) return;

  if (n < 10)
    lcd.setCursor(offset + 1, line);
  else
    lcd.setCursor(offset, line);
  lcd.print(n);
}
void lcd_print_min_sec(float minutes, unsigned line=0, int offset=0)
{
  int min = (int) minutes;
  int sec = (int) (60 * (minutes - floor(minutes)));
  int pos = offset;// = (min < 10) ? offset + 1 : offset;

  lcd.setCursor(pos, line);
  if (min < 10) {
    lcd.print(" ");
    pos += 1;
    lcd.setCursor(pos, line);
  }
  lcd.print(min);

  pos = (min < 10) ? pos + 1 : pos + 2;
  lcd.setCursor(pos, line);
  lcd.print(":");
  pos += 1;
  if (sec < 10) {
    lcd.print("0");
    lcd.setCursor(pos+1, line);
  }
  lcd.print(sec);
}

void set_led_timer(const RgbColor &color, uint16_t setNumLeds)
{
  for (uint16_t pixel = 0; pixel < pixelCount; pixel++) {
    strip.SetPixelColor(pixel, color);
  }
  delay(10);
  // from 0 to led_timer value
  for (uint16_t pixel = 0; pixel < (pixelCount - setNumLeds); pixel++) {
    strip.SetPixelColor(pixel, black);
  }
  strip.Show();
}

void set_led_timeout(const RgbColor &color)
{
  if (timeoutToggle) {
    for (uint16_t pixel = 0; pixel < pixelCount; pixel += 2) {
      strip.SetPixelColor(pixel, color);
    }
  }
  else {
    for (uint16_t pixel = 0; pixel < pixelCount; ++pixel) {
      strip.SetPixelColor(pixel, black);
    }
  }
  strip.Show();
  timeoutToggle = !timeoutToggle;
}

void clear_leds()
{
  for (uint16_t pixel = 0; pixel <= pixelCount; pixel++) {
    strip.SetPixelColor(pixel, black);
  }
  strip.Show();
};


void setup()
{
  Serial.begin(115200);
  Serial.println();
  Serial.println("SCRUM TIMER");
  Serial.print("Version: ");
  Serial.println(VERSION);
    
  boolean rc = WiFi.softAP(ssid, password);
  if (true == rc)
    Serial.println("AP Ready");
  else
    Serial.println("Soft AP setup Failed!");

  // this resets all the neopixels to an off state
  strip.Begin();
  strip.Show();
  // quick test
  for (uint16_t pixel = 0; pixel <= pixelCount; pixel++) {
    strip.SetPixelColor(pixel, blue);
    if (pixel > 0)
      strip.SetPixelColor(pixel - 1, black);
    strip.Show();
    delay(20);
  }



  ArduinoOTA.setHostname("scrumtimer1");
  ArduinoOTA.onStart([]() {
		       String type;
		       if (ArduinoOTA.getCommand() == U_FLASH) {
			 type = "sketch";
		       } else { // U_FS
			 type = "filesystem";
		       }

		       // NOTE: if updating FS this would be the place to unmount FS using FS.end()
		       Serial.println("Start updating " + type);
		     });
  ArduinoOTA.onEnd([]() {
		     Serial.println("\nEnd");
		   });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
			  Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
			});
  ArduinoOTA.onError([](ota_error_t error) {
		       Serial.printf("Error[%u]: ", error);
		       if (error == OTA_AUTH_ERROR) {
			 Serial.println("Auth Failed");
		       } else if (error == OTA_BEGIN_ERROR) {
			 Serial.println("Begin Failed");
		       } else if (error == OTA_CONNECT_ERROR) {
			 Serial.println("Connect Failed");
		       } else if (error == OTA_RECEIVE_ERROR) {
			 Serial.println("Receive Failed");
		       } else if (error == OTA_END_ERROR) {
			 Serial.println("End Failed");
		       }
		     });
  ArduinoOTA.begin();

  // pin config
  pinMode(LED_BUILTIN, OUTPUT);

  // inputs button 1-4
  pinMode(button1, INPUT_PULLUP);
  pinMode(button2, INPUT_PULLUP);
  pinMode(button3, INPUT_PULLUP);
  pinMode(button4, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(button1), button1_cb, FALLING);
  attachInterrupt(digitalPinToInterrupt(button2), button2_cb, FALLING);
  attachInterrupt(digitalPinToInterrupt(button3), button3_cb, FALLING);
  attachInterrupt(digitalPinToInterrupt(button4), button4_cb, FALLING);


  lcd.init();
  // Print a message to the LCD.
  lcd.backlight();

  // led off
  digitalWrite(LED_BUILTIN, HIGH);


}


void loop()
{
  if (POR == mode) {
    timeoutToggle = false;
    clear_leds();
    lcd.setCursor(0, 0);
    lcd.print(" *SCRUM  TIMER* ");
    lcd.setCursor(0, 1);
    lcd.print("Version:       ");
    lcd.setCursor(9, 1);
    lcd.print(VERSION);
  }

  // menu ... button 1, cycle through config mode(s) 1..3
  if (HIGH == b1_state) {
    Serial.println("button 1");
    b1_state = LOW;

    clear_leds();

    if (RUNNING == mode) {
      // meeting started, stop if button 1 pressed
      Serial.println("coming from mode=4 reset mode to 0!");
      currentPerson = 1;
      mode = POR;
      return;
    }

    mode++;
    if (CONFIG == mode) mode = ENTER_PERSON;
    lcd.setCursor(0, 0);
    if (ENTER_PERSON == mode) {
      lcd.print("Enter Persons:  ");
      lcd.setCursor(0, 1);
      lcd.print("   persons      ");
      lcd_print_num(persons, 1);
    }

    if (ENTER_TIME == mode) {
      lcd.print("Enter Time:     ");
      lcd.setCursor(0, 1);
      lcd.print("   minutes      ");
      lcd_print_num(scrumTime, 1);
    }

    if (SHOW_T_PER_PERSON == mode) {
      lcd.setCursor(0, 0);
      lcd.print("Min. per person:");
      lcd.setCursor(0, 1);
      lcd.print("                ");
      lcd.setCursor(0, 1);
      lcd.print(t_per_person);
    }
  }

  if (HIGH == b2_state) { // up/increase
    Serial.println("button 2");
    if (ENTER_TIME == mode) { // enter time
      if (scrumTime < 99) scrumTime++;
      lcd.setCursor(0, 1);
      lcd.print("   minutes      ");
      lcd_print_num(scrumTime, 1);
    }
    if (ENTER_PERSON == mode) { // enter persons
      if (persons < 99) persons++;
      lcd.setCursor(0, 1);
      lcd.print("   persons      ");
      lcd_print_num(persons, 1);
    }
    t_per_person = (float)scrumTime / (float)persons;
    first_warn_t = firstWarn * t_per_person;
    final_warn_t = finalWarn * t_per_person;
    b2_state = LOW;
  }

  if (HIGH == b3_state) { // down/decrease
    Serial.println("button 3");
    if (ENTER_TIME == mode) { // enter time
      if (scrumTime > 1) scrumTime--;
      lcd.setCursor(0, 1);
      lcd.print("   minutes      ");
      lcd_print_num(scrumTime, 1);
    }
    if (ENTER_PERSON == mode) { // enter persons
      if (persons > 2) persons--;
      lcd.setCursor(0, 1);
      lcd.print("   persons      ");
      lcd_print_num(persons, 1);
    }
    t_per_person = (float)scrumTime / (float)persons;
    first_warn_t = firstWarn * t_per_person;
    final_warn_t = finalWarn * t_per_person;
    b3_state = LOW;
  }


  if (HIGH == b4_state) { // start scrum / next person
    Serial.println("button 4");
    b4_state = LOW;
    timeout = 0;
    timeoutToggle = false;

    for (uint16_t pixel = 0; pixel < pixelCount; pixel++) {
      strip.SetPixelColor(pixel, green);
    }
    strip.Show();

    if (mode != RUNNING) {
      mode = RUNNING;
      timer = t_per_person;

      lcd.setCursor(0, 0);
      lcd.print("Start, press NXT");
      lcd.setCursor(0, 1);
      lcd.print("  :       min.  ");
      lcd_print_num(currentPerson, 1);
      lcd_print_min_sec(timer, 1, 4);
    }
    else {
      timer = t_per_person;
      currentPerson++;
      if (currentPerson > persons) {
	currentPerson = 1;
	mode = POR;
	return;
      }

      lcd.setCursor(0, 0);
      lcd.print("  NEXT !!!      ");
      lcd.setCursor(0, 1);
      lcd.print("  :       min.  ");
      lcd_print_num(currentPerson, 1);
      lcd_print_min_sec(timer, 1, 4);
    }
  }


  // update timer, once a second
  if (millis() > (time_1 + EVERY_SECOND)) {
    time_1 = millis();
    // timer is in decimal minutes, decrement every second
    timer -= 0.0167; // 1/60
    ledTimer = timer * (pixelCount / t_per_person);

    if (timer <= 0.0) {
      timer = 0.0;
      if (RUNNING == mode) {
	timeout = 1;
	lcd.setCursor(0, 0);
	lcd.print(" !!! TIMEOUT !!!");
	Serial.println("TIMEOUT");
	set_led_timeout(red);
      }
    }
    else if (RUNNING == mode) {
      lcd_print_min_sec(timer, 1, 4);

      if (timer <= first_warn_t)
	set_led_timer(red, ledTimer);
      // else if (timer <= first_warn_t)
      //	set_led_timer(yellow,  ledTimer);
      else
	set_led_timer(green, ledTimer);

      Serial.println(timer);
      Serial.println(ledTimer);
    }
  }

  ArduinoOTA.handle();
}
