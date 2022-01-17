
/*
 * Copyright (c) 2019,2020 Andreas Loeffler <al@exitzero.de>
 */

// currently using WEMOS D1 mini (esp8266

#include <NeoPixelBus.h>
#include <NeoPixelAnimator.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <Button2.h>
//#include <time.h>

// TODO: config option via wifi/webserver (AP running now)
//       eg. to set light strip size or type (strip, ring, matrix)
//       store defaults in flash and add config menu to change defaults
//       possible other "apps": (tea-)timer, clock, ...
#include <ArduinoOTA.h>
//#include <ESP8266WebServer.h>

const char* ssid = "SCRUMTIMER";
const char* password = "m1u2r3c4s5";

const char* VERSION = "1.11";


#define SET_DD_POS()    lcd.setCursor( 3, 0)
#define SET_MO_POS()    lcd.setCursor( 6, 0)
#define SET_YY_POS()    lcd.setCursor( 9, 0)
#define SET_HH_POS()    lcd.setCursor( 4, 1)
#define SET_MM_POS()    lcd.setCursor( 7, 1)
#define SET_SS_POS()    lcd.setCursor(10, 1)

#define PRINT_2DIGIT(N) do {                    \
    if (N < 10)                                 \
      lcd.print("0");                           \
    lcd.print(N);                               \
  } while (0)


// rgb strip configuration
// leds on strip/ring
const uint16_t pixelCount = 60;
 // ignored for Esp8266 (always uses RX pin)
const uint8_t  pixelPin = RX;
// aka brightness (max 255)
uint8_t colorSaturation = 64; // todo: config option for brightness
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
volatile uint8_t b1State = LOW;
volatile uint8_t b2State = LOW;
volatile uint8_t b3State = LOW;
volatile uint8_t b4State = LOW;
volatile unsigned long lastButton1 = 0;
volatile unsigned long lastButton2 = 0;
volatile unsigned long lastButton3 = 0;
volatile unsigned long lastButton4 = 0;
bool OTA_ENABLED = false;

// button debounce time in ms
unsigned long debounceT = 350;

// todo: maybe switch to Button2 lib
//Button2 btn1(button1);
//Button2 btn2(button2);
//Button2 btn3(button3);
//Button2 btn4(button4);


// mode or state where the timer is currently running in
enum modes {
  POR = 0,
  ENTER_PERSON,
  ENTER_TIME,
  SHOW_T_PER_PERSON,
  CLOCK,
  SET_RTC,
  CONFIG,
  RUNNING,
  TIMEOUT
};

enum set_time_state {
  SET_DAY = 0,
  SET_MONTH,
  SET_YEAR,
  SET_HOUR,
  SET_MIN,
  SET_SEC,
  _NUM_SET_STATES_
};

uint8_t mode = POR;
uint8_t set_mode = SET_DAY;
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
#define EVERY_500ms   500
unsigned long time_1 = 0;
unsigned long time_2 = 0;


// FIXME: define constant for lcd layout lines/cols
LiquidCrystal_I2C lcd(0x3F, 16, 2);
NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> strip(pixelCount, pixelPin);
NeoPixelAnimator animations(1, NEO_MILLISECONDS); // one animation for timeout

RTC_DS1307 rtc;
DateTime prev, set_DateTime;
int hh = 0, mm = 0, ss = 0, dd = 0, mo = 0, yy = 0;


// put isr's into ram, esp might crash otherwise (due to mutlithrading)
IRAM_ATTR void button1_cb()
{
  if ((millis() - lastButton1) > debounceT) {
    b1State = HIGH;
    lastButton1 = millis();
  }
}
IRAM_ATTR void button2_cb()
{
  if ((millis() - lastButton2) > debounceT) {
    b2State = HIGH;
    lastButton2 = millis();
  }
}
IRAM_ATTR void button3_cb()
{
  if ((millis() - lastButton3) > debounceT) {
    b3State = HIGH;
    lastButton3 = millis();
  }
}
IRAM_ATTR void button4_cb()
{
  if ((millis() - lastButton4) > debounceT) {
    b4State = HIGH;
    lastButton4 = millis();
  }
}

// https://create.arduino.cc/projecthub/LuckyResistor/c-templates-for-embedded-code-95b42b
template<typename Val>
void lcd_print_num(Val n, unsigned line=0, int offset=0, bool leadZero=false)
{
  if (line > 1) return;

  if (n < 10) {
    if (leadZero) {
      lcd.setCursor(offset, line);
      lcd.print("0");
    }
    lcd.setCursor(offset + 1, line);
  }
  else
    lcd.setCursor(offset, line);
  lcd.print(n);
}

// void lcd_print_dd(uint8_t n) {
//   SET_DD_POS();
//   PRINT_2DIGIT(n);
// }

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

void lcd_print_date_time(bool clear_all=false)
{
  DateTime now = rtc.now();
  char date_buf[] = "DD.MM.YYYY";
  char time_buf[] = "hh:mm:ss";

  if (!now.isValid()) {
    int retry = 3;
    clear_all = true;
    while (!now.isValid() || (retry-- > 0)) {
      now = rtc.now();
      lcd.setCursor(0, 0);
      lcd.print(retry);
      delay(200);
    }
  }

  if (clear_all) {
    lcd.clear();
    SET_DD_POS();
    lcd.print(now.toString(date_buf));
    SET_HH_POS();
    lcd.print(now.toString(time_buf));
    return;
  }

  if (prev == now) return;

  if (now.second() != prev.second()) {
    SET_SS_POS();
    PRINT_2DIGIT(now.second());
  }
  if (now.minute() != prev.minute()) {
    SET_MM_POS();
    PRINT_2DIGIT(now.minute());
  }
  if (now.hour() != prev.hour()) {
    SET_HH_POS();
    PRINT_2DIGIT(now.hour());
  }
  if (now.day() != prev.day()) {
    SET_DD_POS();
    PRINT_2DIGIT(now.day());
  }
  if (now.month() != prev.month()) {
    SET_MO_POS();
    PRINT_2DIGIT(now.month());
  }
  if (now.year() != prev.year()) {
    SET_YY_POS();
    lcd.print(now.year());
  }

  // lcd.setCursor(0, 0);
  // lcd.print("*");
  // lcd.setCursor(15, 0);
  // lcd.print("*");
  // lcd.setCursor(0, 1);
  // lcd.print("*");
  // lcd.setCursor(15, 1);
  // lcd.print("*");
  prev = now;
}

void lcd_blink_date_time()
{
  static bool toggle = true;
  lcd.clear();

  SET_SS_POS();
  PRINT_2DIGIT(ss);
  SET_MM_POS();
  PRINT_2DIGIT(mm);
  SET_HH_POS();
  PRINT_2DIGIT(hh);
  SET_DD_POS();
  PRINT_2DIGIT(dd);
  SET_MO_POS();
  PRINT_2DIGIT(mo);
  SET_YY_POS();
  lcd.print(yy);

  if (toggle) {
    switch (set_mode) {
    case SET_DAY  :
      SET_DD_POS();
      lcd.print("  ");
      break;
    case SET_MONTH:
      SET_MO_POS();
      lcd.print("  ");
      break;
    case SET_YEAR :
      SET_YY_POS();
      lcd.print("    ");
      break;
    case SET_SEC  :
      SET_SS_POS();
      lcd.print("  ");
      break;
    case SET_MIN  :
      SET_MM_POS();
      lcd.print("  ");
      break;
    case SET_HOUR :
      SET_HH_POS();
      lcd.print("  ");
      break;
    default:
      break;
    }
  }
  toggle = !toggle;
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

  // pin config
  pinMode(LED_BUILTIN, OUTPUT);
  // inputs button 1-4
  /* FIXME: HW add external resistors for strong pull-up ...
   * or use pull-downs in order to prevent unexpected gpio toggles
   */
  pinMode(button1, INPUT_PULLUP);
  pinMode(button2, INPUT_PULLUP);
  pinMode(button3, INPUT_PULLUP);
  pinMode(button4, INPUT_PULLUP);

  // enable lcd display
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print(" *SCRUM  TIMER* ");
  lcd.setCursor(0, 1);
  lcd.print("Version:       ");
  lcd.setCursor(9, 1);
  lcd.print(VERSION);
  delay(1000);

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

  if (! rtc.begin()) {
    Serial.println("Couldn't find RTC");
    Serial.flush();
    lcd.setCursor(0, 0);
    lcd.print("**RTC ERROR !!**");
  }

  if (! rtc.isrunning()) {
    Serial.println("RTC is NOT running, let's set the time!");
    lcd.setCursor(0, 0);
    lcd.print(" RTC NOT RUN    ");
    lcd.setCursor(0, 1);
    lcd.print(" RTC SetTime !! ");

    // When time needs to be set on a new device, or after a power loss, the
    // following line sets the RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    // This line sets the RTC with an explicit date & time, for example to set
    // January 21, 2014 at 3am you would call:
    // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
  }

  lcd.setCursor(0, 0);
  lcd.print("**RTC TIME OK** ");
  delay(500);
  lcd_print_date_time(true);
  delay(2000);

  // enable AP/OTA mode only if powered up while a key pressed
  WiFi.softAPdisconnect(true); // deconfigure and disable
  WiFi.enableAP(false); // disable per default

  //delay(1000);

  if (LOW == digitalRead(button4)) {
    lcd.setCursor(0, 0);
    lcd.print(" enable AP/OTA  ");

    WiFi.enableAP(true);
    if (WiFi.softAP(ssid, password)) {
      Serial.println("AP Ready");
      lcd.setCursor(0, 1);
      lcd.print(" AP/OTA ready   ");
      OTA_ENABLED = true;
    }
    else {
      Serial.println("Soft AP setup Failed!");
      lcd.setCursor(0, 1);
      lcd.print(" AP seup failed ");
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
			 clear_leds();
		       });
    ArduinoOTA.onEnd([]() {
		       Serial.println("\nEnd");
		     });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
			    unsigned int percent = progress / (total / 100);
			    unsigned int pixel = ((unsigned int)(percent * 0.6)) - 1;
			    Serial.printf("Progress: %u%%\r", percent);
			    // upload progress on ring
			    strip.SetPixelColor(pixel, blue);
			    strip.Show();

			    lcd.setCursor(0, 0);
			    lcd.print(" * OTA UPDATE * ");
			    lcd.setCursor(0, 1);
			    lcd.print("       /       B");
			    lcd.setCursor(0, 1);
			    lcd.print(progress);
			    lcd.setCursor(8, 1);
			    lcd.print(total);
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
  }

  attachInterrupt(digitalPinToInterrupt(button1), button1_cb, FALLING);
  attachInterrupt(digitalPinToInterrupt(button2), button2_cb, FALLING);
  attachInterrupt(digitalPinToInterrupt(button3), button3_cb, FALLING);
  attachInterrupt(digitalPinToInterrupt(button4), button4_cb, FALLING);

  // blue on-board led off
  digitalWrite(LED_BUILTIN, HIGH);
  clear_leds();

  delay(500); // time to check lcd message
}


void loop()
{
  if (true == OTA_ENABLED)
    ArduinoOTA.handle();

  // menu ... button 1, cycle through config mode(s) 1..3
  if (HIGH == b1State) {
    Serial.println("button 1");
    b1State = LOW;

    clear_leds();

    if (RUNNING == mode) {
      // meeting started, stop if button 1 pressed
      Serial.println("coming from mode=4 reset mode to 0!");
      currentPerson = 1;
      mode = POR;
      return;
    }

    mode++;
    if (CONFIG == mode) {
      mode = ENTER_PERSON; // "reset" mode
    }

    lcd.setCursor(0, 0);
    if (ENTER_PERSON == mode) {
      lcd.clear();
      lcd.print("Enter Persons:  ");
      lcd.setCursor(0, 1);
      lcd.print("   persons      ");
      lcd_print_num(persons, 1);
    }

    if (ENTER_TIME == mode) {
      lcd.clear();
      lcd.print("Enter Time:     ");
      lcd.setCursor(0, 1);
      lcd.print("   minutes      ");
      lcd_print_num(scrumTime, 1);
    }

    if (SHOW_T_PER_PERSON == mode) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Min. per person:");
      lcd.setCursor(0, 1);
      lcd.print("                ");
      lcd.setCursor(0, 1);
      lcd.print(t_per_person);
    }
    if (CLOCK == mode) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("   ** CLOCK **  ");
      delay(1000);
      lcd_print_date_time(true);
      return;
    }
    if (SET_RTC == mode) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("  Set rtc clock ");
      set_mode = SET_DAY;
      set_DateTime = rtc.now();
      ss = set_DateTime.second();
      mm = set_DateTime.minute();
      hh = set_DateTime.hour();
      dd = set_DateTime.day();
      mo = set_DateTime.month();
      yy = set_DateTime.year();

      delay(1000);
      return;
    }
  } // button1

  if (HIGH == b2State) { // up/increase
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
    if (SET_SEC   == set_mode) { ss += 1; if (ss > 59)   ss = 0;    }
    if (SET_MIN   == set_mode) { mm += 1; if (mm > 59)   mm = 0;    }
    if (SET_HOUR  == set_mode) { hh += 1; if (hh > 23)   hh = 0;    }
    if (SET_DAY   == set_mode) { dd += 1; if (dd > 31)   dd = 1;    }
    if (SET_MONTH == set_mode) { mo += 1; if (mo > 12)   mo = 1;    }
    if (SET_YEAR  == set_mode) { yy += 1; if (yy > 2050) yy = 2000; }


    t_per_person = (float)scrumTime / (float)persons;
    first_warn_t = firstWarn * t_per_person;
    final_warn_t = finalWarn * t_per_person;
    b2State = LOW;
  } // button 2

  if (HIGH == b3State) { // down/decrease
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

    if (SET_SEC   == set_mode) { ss -= 1; if (ss < 0)   ss = 59;    }
    if (SET_MIN   == set_mode) { mm -= 1; if (mm < 0)   mm = 59;    }
    if (SET_HOUR  == set_mode) { hh -= 1; if (hh < 0)   hh = 23;    }
    if (SET_DAY   == set_mode) { dd -= 1; if (dd < 0)   dd = 31;    }
    if (SET_MONTH == set_mode) { mo -= 1; if (mo < 0)   mo = 12;    }
    if (SET_YEAR  == set_mode) { yy -= 1; if (yy < 2000) yy = 2050; }


    t_per_person = (float)scrumTime / (float)persons;
    first_warn_t = firstWarn * t_per_person;
    final_warn_t = finalWarn * t_per_person;
    b3State = LOW;
  } // button3


  if (HIGH == b4State) { // start scrum / next person
    Serial.println("button 4");
    b4State = LOW;

    if (SET_RTC == mode) {
      // if in set rtc mode then button 4 cycles through dd,mm,yyyy,hh,min,sec,..
      set_mode++;
      if (set_mode > _NUM_SET_STATES_) {
	set_mode = SET_DAY;
	mode = CLOCK;
	rtc.adjust(DateTime(yy, mo, dd, hh, mm, ss));
      }
      lcd.setCursor(0, 1); lcd.print(set_mode);
      return;
    }

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
  } // button4

  /*******************************************/

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

  if (SET_RTC == mode) {
    // make the the digit blink that we want to set ...
    if (millis() > (time_2 + EVERY_500ms)) {
      time_2 = millis();
      lcd_blink_date_time();
    }
  }


  /***** UPDATE TIMER, ONCE A SECOND *****/
  if (millis() > (time_1 + EVERY_SECOND)) {
    time_1 = millis();

    if (CLOCK == mode) {
      lcd_print_date_time();
    }
    else {
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
  } // every second
}
