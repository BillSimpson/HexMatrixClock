#include <RTClib.h>
#include <FastLED.h>
#include "Adafruit_VEML7700.h"

FASTLED_USING_NAMESPACE

// This is the hex matrix clock version 4
// Bill Simpson Nov 2020-Jan 2021
//
// This uses FastLED library for the display
// The matrix has 13 columns with four or five LEDs in the column
// 
// Serial connection at  commands BAUDRATE
//
// ####S = Set time to HHMM
// ####C = Enter Conway Mode for argument milliseconds
// #W = Swipeoff in argument direction
// D = Set default colors
// ###H = Set hue for display pattern (0...255), FastLED spectrum HSV colorspace
// ###I = Set "inverse" hue for display pattern (0...255), FastLED spectrum HSV colorspace
// V or ? = Show version, time, and temperature
// ####X[20] = Transfer binary hexgrid.  The pre-argument is the number of milliseconds
//        to hold the pattern, and the 20 characters after the X are the pattern, in 
//        16 bit words (2 characters) x 10 rows, MSB,LSB order.
//        When the pattern times out, the display returns to the clock or temperature.
//        In principle at 9600 baud, the transferred hexgrids could go as fast as 60 Hz
//        These binary masks are displayed without two-tone colors, only fg1 and bg

#define VERSION        F("HexMatrixClock Version 4.1\0")
#define DATA_PIN       6
#define LED_TYPE       WS2811
#define COLOR_ORDER    GRB
#define NUM_LEDS       58
#define BAUDRATE       9600
#define BRIGHTNESS     96
#define FRAMES_PER_SECOND 120
#define BUFFLEN        22
#define STEP_MILLIS    2000
#define DEFAULT_HUE    50
#define XFER_TIMEOUT   500
#define MIN_Q          -7
#define MAX_Q          5
#define MIN_R          -4
#define MAX_R          5
#define HGR_ROWS       (MAX_R - MIN_R) + 1
#define BITMASK_LENGTH (MAX_Q - MIN_Q) + 1 
#define START_R        2
#define LUX_NAVG       10
#define GESTURE_HOLD_MILLIS 3000
#define TEMP_HOLD_MILLIS    4000
#define TRANSITION_MILLIS   4000
#define GESTURE_RATIO  2
#define UNIT_C         0
#define UNIT_F         1
#define Q_2COLOR_TIME  -2
#define Q_2COLOR_TEMP  2
#define NUM_DIRS       6
#define LOW_LUX        0.5
#define WALK_PAINT     0
#define WALK_CONWAY    1
#define WALK_SYMMETRY  2
#define SYMMETRY_INVERSION    4
#define SYMMETRY_MIRROR_V     2
#define SYMMETRY_MIRROR_H     1

// define transitions enum
enum transition_type { abrupt, conway, swipeoff };
enum info_type { hhmm, temp_c, temp_f, xfer };
enum colormode_type { twotone, mono, huewave };

// objects
RTC_DS3231 rtc;
DateTime curr_time;
CRGB leds[NUM_LEDS], fg1_crgb, fg2_crgb, bg_crgb;
Adafruit_VEML7700 veml = Adafruit_VEML7700();

// global variables
unsigned long next_millis = 0;
unsigned long timeout_millis = 0;
unsigned long change_unit_millis = 0;
unsigned long xfer_hold_millis = XFER_TIMEOUT;
word curr_hgr[HGR_ROWS], last_hgr[HGR_ROWS];
int ix, buffer_pos = 0, xfer_target = 0;
int hr, timeint;
char inBuffer[BUFFLEN] = "\0";
long argument;           // command argument (long signed integer)
char commandchar = 'v';  // command character
float avg_lux, curr_lux;
unsigned long gesture_hold;
transition_type transition_mode = abrupt;
info_type display_mode = hhmm;
colormode_type color_mode = twotone;
int swipedir;
int q_2color = Q_2COLOR_TIME;
uint8_t curr_hue = DEFAULT_HUE;
uint8_t hue_shift;
uint8_t curr_symmetry;

// The display is 3.1 digits, and in hex coordinates, from right to left, the origin q and r are:
// Note that the last digit is can only display 1 or 0
int place_q_origin[] = {3, -1, -5, -7};
int place_r_origin[] = {-3, -1, 1, 2};

// to write the character bitmaps into hex coordinates, we follow char_hex_offsets order
int char_q_offset[] = {0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2};
int char_r_offset[] = {0, 1, 2, 3, 3, 2, 1, 0, -1, -1, 0, 1, 2};

// there are 6 cardinal directions for shifts and neighbor counting
int dir_q[] = {0, 1, 1, 0, -1, -1};
int dir_r[] = {-1, -1, 0, 1, 1, 0};

const uint16_t PROGMEM digit_bitmasks[] = {
        0b1111100011111,  // 0
        0b0000111110000,  // 1 ...
        0b1011101011101,
        0b1111101011001,
        0b1111001000011,
        0b1101101011011,
        0b1101101011111,
        0b0111100110001,
        0b1111101011111,
        0b1111101011011,  // ... 9
        0b1111101001111,  // A
        0b1100001011111,  // b
        0b1001100011111,  // C
        0b1111001011100,  // d
        0b1001101011111,  // E
        0b0001101001111   // F
};

int hgr_read(word* hgr, int q, int r) {
  return(bitRead(hgr[r-MIN_R],q-MIN_Q));
}

int hgr_read_bounds(word* hgr, int q, int r) {
  if ( (q >= MIN_Q) and (q <= MAX_Q) and (r >= MIN_R) and (r <= MAX_R) ) return(bitRead(hgr[r-MIN_R],q-MIN_Q));
  else return(0);
}

void hgr_write(word* hgr, int q, int r, int b) {
  if ( (q >= MIN_Q) and (q <= MAX_Q) and (r >= MIN_R) and (r <= MAX_R) ) bitWrite(hgr[r-MIN_R],q-MIN_Q,b);
}

void hgr_wipe(word *hgr) {
  int ix;

  for (ix=0; ix<HGR_ROWS; ix++) {
    hgr[ix] = 0;
  }
}

void display_digit_hgr(int digit, int place) {
  int ix, q, r, b;

  if (digit < 0) digit = 0;
  if (digit > 0xF) digit = 0xF;
  if (place < 3) { // it is a digit 0..F; else if in place 4, it is a 0 or 1
    for (ix = 0; ix<BITMASK_LENGTH; ix++) {
      q = place_q_origin[place] + char_q_offset[ix];
      r = place_r_origin[place] + char_r_offset[ix];
      b = bitRead(pgm_read_word_near(digit_bitmasks + digit), ix);
      hgr_write(curr_hgr, q, r, b);
    }
  } 
  if ( (place == 3) and (digit == 1) ) {
    for (ix = 0; ix<4; ix++) {  // this is left most digit (only 1 is possible)
      q = place_q_origin[place];
      r = place_r_origin[place] + ix;
      hgr_write(curr_hgr, q, r, 1);        
    }
  }
} 

void set_hue_based_colors(uint8_t hue) {
  // hue is 8 bits, 0..255
  // around 64 is yellow
  fg1_crgb = CHSV(hue, 255, 255);  // full value and full saturation (rich color, bright)
  fg2_crgb = CHSV(hue, 85, 255);  // full value and 33% saturation (whiter, bright)
  bg_crgb = CHSV(hue+128, 255, 51);  // 20% value and full saturation (opposite color, dim)
}

void set_inverse_hue_colors(uint8_t hue) {
  // hue is 8 bits, 0..255
  // around 64 is yellow
  fg1_crgb = CHSV(hue, 255, 255);  // full value and full saturation (rich color, bright)
  fg2_crgb = CHSV(hue, 85, 255);  // full value and 33% saturation (whiter, bright)
  bg_crgb = CHSV(hue+128, 255, 255);  // full value and full saturation (opposite color, bright)
}

void paint(int lednum, int q, int r) {
  CRGB result;
  
  if (hgr_read(curr_hgr, q, r) == 0) {
    result = bg_crgb;           // background color
  } 
  else {
    switch (color_mode) {
      case mono:
        result = fg1_crgb;     // color fg1 in all places
        break;
      case twotone:
        if (q < q_2color) result = fg2_crgb;   // color for hours area (fg2)
        else result = fg1_crgb;         // color for minutes area (fg1)
        break;
      case huewave:
        result = CHSV(curr_hue + quadwave8(hue_shift)/8 -16 ,255, 255); 
        break;
    }
  }
  leds[lednum] = result;
}

void check_symmetry(int q, int r) {
  if (curr_symmetry == 0) return;
  int state_qr = hgr_read(curr_hgr, q, r);
  if ((curr_symmetry & SYMMETRY_INVERSION) > 0) {// search to check inversion
    if (state_qr != hgr_read_bounds(curr_hgr, -q, -r) ) {
      curr_symmetry = curr_symmetry - SYMMETRY_INVERSION;  
//      Serial.println(F("inversion symmetry failed"));
    }
  }
  if ((curr_symmetry & SYMMETRY_MIRROR_V) > 0) { // search to check vertical mirror symmetry (left/right)
    if (state_qr != hgr_read_bounds(curr_hgr, -q, (r+q)) ) { 
      curr_symmetry = curr_symmetry - SYMMETRY_MIRROR_V;  
//      Serial.println(F("vertical mirror symmetry failed"));
    }
  }
  if ((curr_symmetry & SYMMETRY_MIRROR_H) > 0) { // search to check horizontal mirror symmetry (top/bottom)
    if (state_qr != hgr_read_bounds(curr_hgr, q, (-1*(r+q)) ) ) {
      curr_symmetry = curr_symmetry - SYMMETRY_MIRROR_H;  
//      Serial.println(F("horizontal mirror symmetry failed"));
    }
  }
}

void walk_hgr(int walk_type) {
  int i, q, r, lednum;
  
  r = START_R;
  lednum = 0;
  for (q = MIN_Q; q<(MAX_Q+1); q++) {
    if (abs(q) % 2 == 1) {  // this is an odd column, with 4 hexels
      for (i = 0; i<4; i++) {
        switch (walk_type) {
          case WALK_PAINT:
            paint(lednum, q, r);
            break;
          case WALK_CONWAY:
            conway_cell(q,r);
            break;
          case WALK_SYMMETRY:
            check_symmetry(q,r);
            break;
        }
        lednum++;
        r++;
      }
    }
    else {
      for (i = 0; i<5; i++) {
        r--;        
        switch (walk_type) {
          case WALK_PAINT:
            paint(lednum, q, r);
            break;
          case WALK_CONWAY:
            conway_cell(q,r);
            break;
          case WALK_SYMMETRY:
            check_symmetry(q,r);
            break;
        }
        lednum++;
      }
    }
  }
}

void store_hgr() {
  int ix;
  for (ix=0; ix<HGR_ROWS; ix++) {     // store previous hexgrid
    last_hgr[ix] = curr_hgr[ix];
  }
}

int neighbors(word *hgr, int q, int r) {
  int idir, qdir, rdir, count;

  count = 0;
  for (idir=0;idir<NUM_DIRS;idir++) {
    qdir = q + dir_q[idir];
    rdir = r + dir_r[idir];
    // Check if in bounds
    if ( (qdir >= MIN_Q) and (qdir <= MAX_Q) and (rdir >= MIN_R) and (rdir <= MAX_R) ) {
      // in bounds, so check on neighbor
      if (hgr_read(hgr, qdir, rdir) == 1) count++;
    }
  }
  return (count); 
}

void conway_start() {
  color_mode = huewave;
  store_hgr();            // store curr_hgr into last_hgr
  hgr_wipe(curr_hgr);     // clear curr_hgr
}

void check_symmetry_start() {
  // start the search assuming all symmetries exist and then eliminate them during the search
  curr_symmetry = SYMMETRY_INVERSION | SYMMETRY_MIRROR_H | SYMMETRY_MIRROR_V;
}

void conway_cell(int q, int r) {
  if ( neighbors(last_hgr, q, r) == 2 )    // check if exactly two neighbors
    hgr_write(curr_hgr, q, r, 1);
}

void swipeoff_step() {
  int q, r, old_q, old_r, value;

  color_mode = huewave;
  store_hgr();            // store curr_hgr into last_hgr
  hgr_wipe(curr_hgr);     // clear curr_hgr
  for (q=MIN_Q; q<=MAX_Q; q++) {               // walk through all locations in the hgr
    for (r=MIN_R; r<=MAX_R; r++) {
      old_q = q - dir_q[swipedir];
      old_r = r - dir_r[swipedir];
      if ( (old_q < MIN_Q) or (old_q > MAX_Q) or (old_r < MIN_R) or (old_r > MAX_R) ) {
        value = 0;  // turn off if old coordinates are out of bounds
      }
      else {
        value = hgr_read(last_hgr, old_q, old_r);
      }
      hgr_write(curr_hgr, q, r, value);
    }
  }
}

void print_symmetry() {
  Serial.print(F("Current state has "));
  if (curr_symmetry == 0) Serial.print(F("no "));
  if ((curr_symmetry & SYMMETRY_INVERSION) > 0) Serial.print(F("inversion ")); 
  if ((curr_symmetry & SYMMETRY_MIRROR_V) > 0) Serial.print(F("vertical mirror ")); 
  if ((curr_symmetry & SYMMETRY_MIRROR_H) > 0) Serial.print(F("horizontal mirror ")); 
  Serial.println(F("symmetry"));
}

void display_temp(int unit) {
  int curr_temp;
  
  next_millis = millis() + TEMP_HOLD_MILLIS;     // set refresh time forward
  store_hgr();
  hgr_wipe(curr_hgr);
  q_2color = Q_2COLOR_TEMP;

  if (unit == UNIT_F) {
    curr_temp = round(rtc.getTemperature()*9/5 + 32);          
    display_digit_hgr(0xF, 0);               // Show unit as F
  } else {
    curr_temp = round(rtc.getTemperature());
    display_digit_hgr(0xC, 0);               // Show unit as C
  }
  display_digit_hgr(curr_temp % 10, 1);    // temperature ones place
  curr_temp = curr_temp / 10;              // shift digits right
  display_digit_hgr(curr_temp % 10, 2);    // tens place
  display_digit_hgr(curr_temp / 10, 3);    // hundreds place
}

void display_time() {
  next_millis = millis() + STEP_MILLIS;
  store_hgr();
  hgr_wipe(curr_hgr);
  q_2color = Q_2COLOR_TIME;
  
  curr_time = rtc.now();
  hr = curr_time.hour();
  if (hr>12) hr = hr - 12;        // if hour is > 12, convert to 12 hour time
  if (hr==0) hr = 12;             // also for 12 hour time
  display_digit_hgr(curr_time.minute() % 10, 0);   // ones place minutes
  display_digit_hgr(curr_time.minute() / 10, 1);   // tens place minutes
  display_digit_hgr(hr % 10, 2);     // ones place hour
  display_digit_hgr(hr / 10, 3);     // tens place hour
}

void poll_serial() {
  // check if binary transfer timed out
  if ( (xfer_target > 0) and (millis() > timeout_millis) ) { // transfer failed, clear request and buffer
    buffer_pos = 0;
    xfer_target = 0;
    Serial.println(F("Binary transfer timed out"));
    while( Serial.available() > 0) byte junk = Serial.read(); // clear junk from the input buffer
  }
  
  // read input serial data
  while ( (Serial.available() > 0) and ( commandchar == 0 ) ) {
    byte inChar = Serial.read();
    if (xfer_target == 0) {  // if xfer_target == 0, we are in text mode
      if (inChar < 58) {
        // As long as the incoming byte
        // a number or a minus sign, keep accumulating
        // otherwise, it is a command.
        inBuffer[buffer_pos] = (char)inChar;
        buffer_pos++;
        if (buffer_pos >= BUFFLEN) buffer_pos = BUFFLEN - 1;
      }
      // if you get a command, print the buffer,
        // then the string's value as a long int (global variable argument):
      else {
        inBuffer[buffer_pos] = 0;  // terminare byte array with 0
        Serial.print(F("Received <"));
        Serial.print(inBuffer);
        argument = atol(inBuffer);
        Serial.print(F("> as argument, which parsed to: "));
        Serial.print(argument);
        commandchar = (char)inChar;
        Serial.print(F(" for Command: "));
        Serial.println(commandchar);
        buffer_pos = 0;         // clear the buffer for new input:
      }
    }
    else {  // we are in binary mode; add character
      inBuffer[buffer_pos] = inChar;
      buffer_pos++;
      if ( buffer_pos >= xfer_target) { // successful transfer

        next_millis = millis() + xfer_hold_millis;     // set refresh time forward
        store_hgr();
        hgr_wipe(curr_hgr);
        q_2color = 0;
        for (ix=0; ix<HGR_ROWS; ix++) {
          unsigned int msb = (unsigned int) inBuffer[2*ix];
          unsigned int lsb = (unsigned int) inBuffer[2*ix + 1];
          curr_hgr[ix] = msb * 256 + lsb;
        }
        Serial.println(F("Transferred pattern"));
        xfer_target = 0;
        buffer_pos = 0;
        while( Serial.available() > 0) byte junk = Serial.read(); // clear junk from the input buffer
        display_mode = hhmm;    // make the next display a default display
        color_mode = huewave;
      }
    }
  }
}

int parse_command() {
  if (commandchar>0) {
    switch (commandchar) {
      case 'S':
      case 's':
        if ((argument >=0) && (argument < 2360)) { // time is valid
          hr = argument / 100;
          Serial.print(F("Time argument is read as: "));
          Serial.print(hr);
          Serial.print(":");
          if (argument %100 < 10) Serial.print("0");
          Serial.println(argument % 100);          
          // set time
          curr_time = DateTime(2020, 1, 1, hr, (argument % 100) );
          rtc.adjust(curr_time);
          curr_time = rtc.now();
          Serial.print(F("Time set to: "));
          Serial.print(curr_time.hour());
          Serial.print(":");
          if (curr_time.minute() < 10) Serial.print("0");
          Serial.println(curr_time.minute());          
        } else {
          Serial.println(F("input time is out of range, use HHMM as a 4-digit number"));
        }
        break;
      case 'D':
      case 'd':
        Serial.println(F("Setting colors to default")); 
        curr_hue = DEFAULT_HUE;         
        set_hue_based_colors(curr_hue);      
        break;
      case 'H':
      case 'h':
        if ((argument >=0) && (argument < 256)) { // hue is valid
          Serial.print(F("Set foreground hue to "));          
          Serial.println(argument, DEC);
          curr_hue = (uint8_t) argument;         
          set_hue_based_colors(curr_hue);      
        } else {
          Serial.println(F("Hue set failed, send as base 10 number"));
        }
        break;
      case 'I':
      case 'i':
        if ((argument >=0) && (argument < 256)) { // hue is valid
          Serial.print(F("Set inverse hue to "));          
          Serial.println(argument, DEC);
          curr_hue = (uint8_t) argument;         
          set_inverse_hue_colors(curr_hue);      
        } else {
          Serial.println(F("Inverse hue set failed, send as base 10 number"));
        }
        break;
      case 'V':
      case 'v':
      case '?':
        // takes no argument
          Serial.println(VERSION);
          curr_time = rtc.now();
          Serial.print(F("RTC time is: "));
          Serial.print(curr_time.hour());
          Serial.print(":");
          if (curr_time.minute() < 10) Serial.print("0");
          Serial.println(curr_time.minute());
          Serial.print(F("Temperature is "));
          Serial.print(round(rtc.getTemperature()));          
          Serial.println(F(" C"));
          Serial.print(F("Ambient light sensor reads "));
          Serial.print(curr_lux);
          Serial.println(F(" lux"));
        break;
      case 'C':
      case 'c':
        if ((argument > 0) && (argument < 10001 )) { // hold duration is valid
          next_millis = millis() + argument;
        } else {
          next_millis = millis() + TRANSITION_MILLIS;
        }
        transition_mode = conway;
      break;  
      case 'W':
      case 'w':
        if ((argument >= 0) && (argument < NUM_DIRS )) { // direction is valid
          swipedir = argument;
          next_millis = millis() + TRANSITION_MILLIS;
          transition_mode = swipeoff;
        } else {
          Serial.println(F("sWipeoff direction invalid"));
        }
      break;  
      case 'Y':
      case 'y':
        check_symmetry_start();
        walk_hgr(WALK_SYMMETRY);
        print_symmetry();
      break;
      case 'X':
      case 'x':
        if ((argument > 0) && (argument < 10001 )) { // hold duration is valid
          xfer_hold_millis = argument;
        } else {
          xfer_hold_millis = XFER_TIMEOUT;
        }
        timeout_millis = millis() + XFER_TIMEOUT;
        xfer_target = 20;
        Serial.println(F("Entering binary transfer mode for next 20 bytes"));
        break;
      default:
        Serial.println(F("Command not recognized"));
    }
    commandchar = 0;
  }
}

void setup() {
  delay(3000); // 3 second delay for recovery
  
  // tell FastLED about the LED strip configuration
  FastLED.addLeds<LED_TYPE,DATA_PIN,COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalPixelString);
  //FastLED.addLeds<LED_TYPE,DATA_PIN,CLK_PIN,COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);

  // set master brightness control
  FastLED.setBrightness(BRIGHTNESS);
  // set colors to default hue 
  curr_hue = DEFAULT_HUE;
  set_hue_based_colors(curr_hue);

  Serial.begin(BAUDRATE);           // set up Serial library at BAUDRATE  

  Serial.println(F("HexClock starting")); // print that clock is starting

  // start the RTC
  if (! rtc.begin()) {
    Serial.println("Couldn't find RTC");
    Serial.flush();
    abort();
  }

  if (rtc.lostPower()) {
    Serial.println(F("RTC lost power, setting the time to default 00:00"));
    rtc.adjust(DateTime(2020, 1, 1, 0, 0));
  }

  // ambient light sensor for gestures and brightness (VEML7700)
  if (!veml.begin()) {
    Serial.println(F("Ambient light sensor not found"));
    while (1);
  }
  Serial.println(F("Ambient light sensor found"));

  veml.setGain(VEML7700_GAIN_1);                 // gain = 1
  veml.setIntegrationTime(VEML7700_IT_200MS);    // 200 millisecond integration

  gesture_hold = millis();     // set hold timeout to now
  delay(500);                  // to let the light sensor start up
  avg_lux = veml.readLux();    // set initial average
  Serial.println(avg_lux);

  // start random number generator randomly
  randomSeed(analogRead(0));
}

void loop() {
  // get serial input
  poll_serial();
  // try to parse command
  parse_command();

  // measure ambient light and determine if event happend
  EVERY_N_MILLISECONDS(200) {
    curr_lux = veml.readLux();
    avg_lux = (LUX_NAVG-1.)/LUX_NAVG * avg_lux + curr_lux / LUX_NAVG;
    if ( (millis() > gesture_hold) and (curr_lux > LOW_LUX) and ( (curr_lux < avg_lux/GESTURE_RATIO ) or ( curr_lux > GESTURE_RATIO*avg_lux ) ) ) {
      // light is nonzero and there is a major change in intensity (lux)
      gesture_hold = millis() + GESTURE_HOLD_MILLIS; 
      // display temperature
      display_mode = temp_c;
    }
  }

  switch (transition_mode) {
    case conway:
      EVERY_N_MILLISECONDS(333) {
        conway_start();
        walk_hgr(WALK_CONWAY);
      }
      break;
    case swipeoff:
      EVERY_N_MILLISECONDS(333) swipeoff_step();
      break;
    case abrupt:
    default:
     // check if we are within the TRANSITION_MILLIS of the minute boundary
    curr_time = rtc.now();
    if ( curr_time.second() > (60-TRANSITION_MILLIS/1000) ) {
      // determine if the current pattern is symmetric
      check_symmetry_start();
      walk_hgr(WALK_SYMMETRY);
      print_symmetry();
      color_mode = huewave;
      if ((random8() < 128) || (curr_symmetry >0)) {  
        // get a random number to help select if we should play conway or swipeoff
        // if the pattern was found to be symmetric, always play conway
        transition_mode = conway;
      } 
      else {
        transition_mode = swipeoff;
        swipedir = random(NUM_DIRS);    // pick a random direction
      }
      next_millis = millis() + TRANSITION_MILLIS;  // set next update time after minute
    }
  }    

  if (millis() > next_millis) {
    switch (display_mode) {
      case temp_c:
        display_temp(UNIT_C);
        display_mode = temp_f;
        break;
      case temp_f:
        display_temp(UNIT_F);
        display_mode = hhmm;
        break;
      case xfer:
        display_mode = hhmm;
        break;
      default:
      case hhmm:
        display_time();
        display_mode = hhmm;
        break;
    }
    color_mode = twotone;
    transition_mode = abrupt;
  }


  // convert the hexgrid to the LED string, coloring it    
  walk_hgr(WALK_PAINT);
  // send the 'leds' array out to the actual LED strip
  FastLED.show();  
  // insert a delay to keep the framerate modest
  FastLED.delay(1000/FRAMES_PER_SECOND); 
  // update the hue_shift for huewave mode
  hue_shift ++;
}
