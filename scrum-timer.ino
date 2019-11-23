
/*
 * Copyright (c) 2019 Andreas Loeffler <al@exitzero.de>
 */

#include <NeoPixelBus.h>
#include <NeoPixelAnimator.h>

//#include <ESP8266WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>


const char* VERSION = "0.4";


// rgb strip configuration
const uint16_t pixelCount = 32;
const uint8_t  pixelPin = RX; // make sure to set this to the correct pin, ignored for Esp8266

const int button1 = D5; // next / menu / enter 14
const int button2 = D7; // reset / menu ?      13
const int button3 = D6; // up                  12
const int button4 = D4; // down                15


volatile byte b1_state = LOW;
volatile byte b2_state = LOW;
volatile byte b3_state = LOW;
volatile byte b4_state = LOW;
volatile unsigned long lastButton1 = 0;
volatile unsigned long lastButton2 = 0;
volatile unsigned long lastButton3 = 0;
volatile unsigned long lastButton4 = 0;

byte mode = 0; // user input mode(s) vs timer mode
byte debounceT = 500;

enum modes {
  POR = 0,
  ENTER_PERSON,
  ENTER_TIME,
  SHOW_T_PER_PERSON,
  CONFIG,
  RUNNING
};


int scrum_time = 15; // scrum meeting time in minutes, default 15min
int persons = 5; // number of participants, default 5
float t_per_person = (float)scrum_time / (float)persons;
float timer;
uint16_t led_timer;
unsigned current_person = 1;

#define EVERY_SECOND 1000 // every second do...
unsigned long time_1 = 0;


// FIXME: define constant for lcd layout lines/cols
LiquidCrystal_I2C lcd(0x3F, 16, 2);
NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> strip(pixelCount, pixelPin);



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



void setup()
{

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

  Serial.begin(115200);
  Serial.println("SCRUM CLOCK");

  lcd.init();
  // Print a message to the LCD.
  lcd.backlight();

  // led off
  digitalWrite(LED_BUILTIN, HIGH);

  // this resets all the neopixels to an off state
  strip.Begin();
  strip.Show();


  // quick test
  // for (uint16_t pixel = 0; pixel < pixelCount; pixel++) {
  //   RgbColor color = RgbColor(random(255), random(255), random(255));
  //   strip.SetPixelColor(pixel, color);
  // }

}



void loop()
{
  if (POR == mode) {
    lcd.setCursor(0, 0);
    lcd.print(" *SCRUM  CLOCK* ");
    lcd.setCursor(0, 1);
    lcd.print("Version:       ");
    lcd.setCursor(9, 1);
    lcd.print(VERSION);
  }

  // menu
  if (HIGH == b1_state) {
    Serial.println("button 1");
    b1_state = LOW;

    if (RUNNING == mode) {
      // meeting started, stop if button 1 pressed ?!
      Serial.println("coming from mode=4 reset mode to 0!");
      current_person = 1;
      mode = POR;
      return;
    }

    mode++;
    if (CONFIG == mode) mode = ENTER_PERSON; // button 1, cycle through config mode(s) 1..3
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
      lcd_print_num(scrum_time, 1);
    }

    if (SHOW_T_PER_PERSON == mode) {
      lcd.setCursor(0, 0);
      lcd.print("Min.per person: ");
      lcd.setCursor(0, 1);
      lcd.print("                ");
      lcd.setCursor(0, 1);
      lcd.print(t_per_person);
    }

    //if (3 == mode) mode = 0;
  }

  if (HIGH == b2_state) { // up/increase
    Serial.println("button 2");
    if (ENTER_TIME == mode) { // enter time
      if (scrum_time < 99) scrum_time++;
      lcd.setCursor(0, 1);
      lcd.print("   minutes      ");
      lcd_print_num(scrum_time, 1);
    }
    if (ENTER_PERSON == mode) { // enter persons
      if (persons < 99) persons++;
      lcd.setCursor(0, 1);
      lcd.print("   persons      ");
      lcd_print_num(persons, 1);
    }
    t_per_person = (float)scrum_time / (float)persons;
    b2_state = LOW;
  }

  if (HIGH == b3_state) { // down/decrease
    Serial.println("button 3");
    if (ENTER_TIME == mode) { // enter time
      if (scrum_time > 0) scrum_time--;
      lcd.setCursor(0, 1);
      lcd.print("   minutes      ");
      lcd_print_num(scrum_time, 1);
    }
    if (ENTER_PERSON == mode) { // enter persons
      if (persons > 1) persons--;
      lcd.setCursor(0, 1);
      lcd.print("   persons      ");
      lcd_print_num(persons, 1);
    }
    t_per_person = (float)scrum_time / (float)persons;
    b3_state = LOW;
  }


  if (HIGH == b4_state) { // start scrum / next person
    Serial.println("button 4");
    b4_state = LOW;

    if (mode != RUNNING) {
      mode = RUNNING;
      timer = t_per_person;
      // t_per_person -> all leds on (green?)
      //led_timer = timer * (pixelCount / timer);

      lcd.setCursor(0, 0);
      lcd.print("Start, press NXT");
      lcd.setCursor(0, 1);
      lcd.print("  :       min.  ");
      lcd_print_num(current_person, 1);
      //lcd_print_num(timer, 1, 4);
      lcd_print_min_sec(timer, 1, 4);
    }
    else {
      timer = t_per_person;
      current_person++;
      if (current_person > persons) {
	// message
	current_person = 1;
	mode = POR;
	return;
      }

      lcd.setCursor(0, 0);
      lcd.print("  NEXT !!!      ");
      lcd.setCursor(0, 1);
      lcd.print("  :       min.  ");
      lcd_print_num(current_person, 1);
      //lcd_print_num(timer, 1, 4);
      lcd_print_min_sec(timer, 1, 4);
    }
  }


  // update timer
  if (millis() > time_1 + EVERY_SECOND) {
    time_1 = millis();
    // timer is in decimal minutes
    timer -= 0.016;
    led_timer = timer * (pixelCount / t_per_person);

    if (timer < 0.0) {
      timer = 0.0;
      if (RUNNING == mode) {
	lcd.setCursor(0, 0);
	lcd.print(" !!! TIMEOUT !!!");
	Serial.println("TIMEOUT");
      }
    }

    if (RUNNING == mode) {
      lcd_print_min_sec(timer, 1, 4);

      Serial.println(timer);
      Serial.println(led_timer);
    }
  }


}
