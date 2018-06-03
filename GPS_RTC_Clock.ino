/*
 * GPS_RTC_Clock
 * 
 * by Matthew McMillan
 * 
 * This is the code for a little nightstand clock I built. It runs on
 * an Arduino Uno and it uses a GPS signal along with a RTC to keep
 * pretty accurate time.
 * 
 * I used a lot Adafruit libraries on this project but the library that
 * really makes this clock look great is William Zaggle's 'Large 7-Segment'
 * function which can be found at the near the end of this code.
 * 
 * Hardware
 * ---------
 * Display: 2.2" Color TFT LCD which uses the ILI9340 chipset
 * Micro: Arduino Uno
 * GPS: Any GPS breakout that outputs NMEA strings over a serial interface
 * 
 * 
 * matthew.mcmillan@gmail.com
 * @matthewmcmillan
 * http://matthewcmcmillan.blogspot.com/
 * 
 * 
 * 
 * 
 */

#include "SPI.h"
#include "Adafruit_GFX.h"     //https://github.com/adafruit/Adafruit-GFX-Library
#include "Adafruit_ILI9340.h" //https://github.com/adafruit/Adafruit_ILI9340
#include "RTClib.h"           //https://github.com/adafruit/RTClib
#include <DS1307RTC.h>        //http://www.arduino.cc/playground/Code/Time
#include <Time.h>             //https://github.com/PaulStoffregen/Time
#include <Timezone.h>         //https://github.com/JChristensen/Timezone
#include <TinyGPS++.h>        //https://github.com/mikalhart/TinyGPSPlus
#include <SoftwareSerial.h>
#include <Wire.h>

// These are the pins used for the UNO
// for Due/Mega/Leonardo use the hardware SPI pins (which are different)
#define _sclk 13
#define _miso 12
#define _mosi 11
#define _cs 10
#define _rst 9
#define _dc 8


// Serial config for the GPS module
static const int RXPin = 4, TXPin = 3;
static const uint32_t GPSBaud = 9600;

// The TinyGPS++ object
TinyGPSPlus gps;

// The serial connection to the GPS device
SoftwareSerial ss(RXPin, TXPin);

RTC_DS1307 rtc;

Adafruit_ILI9340 tft = Adafruit_ILI9340(_cs, _dc, _rst);

char daysOfTheWeek[7][12] = {"  Sunday  ", "  Monday  ", " Tuesday ", "Wednesday", " Thursday ", "  Friday  ", " Saturday "};

bool rtcSet = false;

//Display backlight settings
int backlightPin = 5;
int backlightVal = 50;

//Number color
int numberColor = ILI9340_WHITE;
int colorChange = 0;

//Photocell
int photocellPin = 0;     // the photocell and 10K pulldown are connected to a0
int photocellReading;     // the analog reading from the analog resistor divider

//Timezone stuff
TimeChangeRule usEDT = {"EDT", Second, Sun, Mar, 2, -240};  //UTC - 4 hours
TimeChangeRule usEST = {"EST", First, Sun, Nov, 2, -300};   //UTC - 5 hours
Timezone usEastern(usEDT, usEST);

TimeChangeRule usCDT = {"CDT", Second, Sun, Mar, 2, -300};    //Daylight time = UTC - 5 hours
TimeChangeRule usCST = {"CST", First, Sun, Nov, 2, -360};     //Standard time = UTC - 6 hours
Timezone usCentral(usCDT, usCST);

TimeChangeRule *tcr;        //pointer to the time change rule, use to get TZ abbrev
time_t utc, local;

//variables for time counting
unsigned long previousMillis = 0;
unsigned long currentMillis = 0;
unsigned long syncTimer = 0;
unsigned long scheduledTimer = 0;

bool newboot = true;

////////////////////////////////////
// SETUP
////////////////////////////////////

void setup()
{
  pinMode(backlightPin, OUTPUT);
  //Turn backlight off until the screen is painted the first time
  analogWrite(backlightPin, 0); 
  
  Serial.begin(115200);
  ss.begin(GPSBaud);

  if (! rtc.begin()) {
    Serial.println(F("Couldn't find RTC"));
    while (1);
  }

  if (! rtc.isrunning()) {
    Serial.println(F("RTC is NOT running!"));
    // following line sets the RTC to the date & time this sketch was compiled
    //rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    // This line sets the RTC with an explicit date & time, for example to set
    // January 21, 2014 at 3am you would call:
    // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
  }

  checkRTCset();
  
  Serial.println();
  Serial.println("---RTC TIME---");
  DateTime now = rtc.now();
  Serial.print("UTC: ");
  Serial.print(now.year(), DEC);
  Serial.print('/');
  Serial.print(now.month(), DEC);
  Serial.print('/');
  Serial.print(now.day(), DEC);
  Serial.print(" (");
  Serial.print(daysOfTheWeek[now.dayOfTheWeek()]);
  Serial.print(") ");
  Serial.print(now.hour(), DEC);
  Serial.print(':');
  Serial.print(now.minute(), DEC);
  Serial.print(':');
  Serial.print(now.second(), DEC);
  Serial.println();

  setSyncProvider(RTC.get);   // the function to get the time from the RTC
    if(timeStatus()!= timeSet) 
        Serial.println(F("Unable to sync with the RTC"));
    else
        Serial.println(F("RTC has set the system time"));

  tft.begin();
  tft.fillScreen(ILI9340_BLACK);

  secondsBorder();

}



////////////////////////////////////
// MAIN LOOP
////////////////////////////////////

void loop()
{ 
  currentMillis = millis();
  if(!rtcSet){
    setRTCfromGPS();
    checkRTCset();
  }

  // Read GPS data from serial connection until no more data is available
  while (ss.available() > 0)
    gps.encode(ss.read());

  if (currentMillis > 5000 && gps.charsProcessed() < 10)
  {
    Serial.println(F("No GPS detected: check wiring."));
    while(true);
  }

  utc = now(); //Grab the current time
  //printTime(utc, "UTC");
  local = usCentral.toLocal(utc, &tcr);
  //printTime(local, tcr -> abbrev);

  
  setNumberColor();
  tftDisplayDate();
  tftDisplayTime();
  secondDotDisplay2();
  tftDisplayGPSsats();
  
  setBackLightBrightness();

  //TO-DO: Add an AM/PM indicator

  syncOnBoot();
  
  //Syncs GPS time to the RTC at 2am.
  scheduledSync();
}



////////////////////////////////////
// FUNCTIONS
////////////////////////////////////

void secondsBorder()
{
  tft.drawFastHLine(0, 72, 240, ILI9340_BLUE);
  tft.drawFastHLine(0, 73, 240, ILI9340_BLUE);
  tft.drawFastHLine(0, 74, 240, ILI9340_BLUE);

  tft.drawFastHLine(0, 90, 240, ILI9340_BLUE);
  tft.drawFastHLine(0, 91, 240, ILI9340_BLUE);
  tft.drawFastHLine(0, 92, 240, ILI9340_BLUE);
}


void secondDotDisplay1()
{
  /*
   * This displays an amimation for the seconds.
   * It moves a dashed line across the display and
   * then blanks it out when seconds go back to zero.
   */
  if(second(local) == 0)
  {
    tft.drawFastHLine(0, 80, 240, ILI9340_BLACK);
    tft.drawFastHLine(0, 81, 240, ILI9340_BLACK);
    tft.drawFastHLine(0, 82, 240, ILI9340_BLACK);
    tft.drawFastHLine(0, 83, 240, ILI9340_BLACK);
  }
  tft.drawFastVLine(second(local)*4, 80, 5, ILI9340_GREEN);
}


void secondDotDisplay2()
{
  /*
   * This displays an amimation for the seconds.
   * It moves a dashed line across the display and
   * then blanks it out as it moves. Less jarring
   * than secondDotDisplay1.
   */

  if(second(local) == 0)
  {
    tft.drawFastVLine(236, 80, 4, ILI9340_BLACK);
    tft.drawFastVLine(232, 80, 4, ILI9340_BLACK);
  }
  tft.drawFastVLine((second(local)*4)-8, 80, 4, ILI9340_BLACK);
  tft.drawFastVLine((second(local)*4)-4, 80, 3, ILI9340_BLACK);
  tft.drawFastVLine(second(local)*4, 80, 5, ILI9340_GREEN);
  tft.drawFastVLine((second(local)*4)+4, 83, 2, ILI9340_GREEN);
}


void setNumberColor()
{
  photocellReading = analogRead(photocellPin);
  
  if((hour(local) >= 20 or hour(local) < 6) and photocellReading <= 100)
  {
    if(numberColor != ILI9340_RED)
    {
      numberColor = ILI9340_RED;
      colorChange = 2;
    }
  }
  else
  {
    if(numberColor != ILI9340_WHITE)
    {
      numberColor = ILI9340_WHITE;
      colorChange = 2;
    }
  }
  
}


void setBackLightBrightness()
{
  /* Takes a reading from the photocell to
   *  get the ambient brightness of a room and
   *  dims the display backlight brightness
   *  as the room gets darker
   */
  photocellReading = analogRead(photocellPin);
  /* DEBUG
  Serial.print("Photocell Analog reading: ");
  Serial.println(photocellReading);     // the raw analog reading
  */
  if (photocellReading > 100)
  {
    backlightVal = 255;
  } 
  if (photocellReading <= 100)
  {
    backlightVal = 50;
  }
  if (photocellReading <= 50)
  {
    backlightVal = 20;
  }
  if (photocellReading < 20)
  {
    backlightVal = 5;
  }

  analogWrite(backlightPin, backlightVal);
}


void tftDisplayDate()
{
  /*  Draws the date on the tft screen
   *  The top line is the day of the week and the
   *  second line is the date in mm/dd/yyyy format
   *  The date is only redrawn for a few minutes after midnight
   *  or within the first few seconds of startup.
   */ 
  if(!rtcSet)
  {
    tft.setCursor(65, 50);
    tft.setTextColor(numberColor, ILI9340_BLACK);  tft.setTextSize(1);
    tft.println("Waiting for GPS Fix");
  }else{
    if((hour(local) == 0 and minute(local) < 5) or millis() < 2500 or colorChange > 0)
    {
      DateTime now = rtc.now();
      tft.setTextColor(numberColor, ILI9340_BLACK);  tft.setTextSize(2);
      //Set starting x for weekday pos. Size 2 characters are 10 pixels wide.
      int daychars = strlen(daysOfTheWeek[weekday(local)-1]);
      int xpos = 120-((daychars/2)*10);
      if(xpos % 2)
      { 
        //If there is an odd number of characters adjust the position by half a
        //character width.
        xpos = xpos - 5;
      } 
      tft.setCursor(xpos, 10);
      tft.print(daysOfTheWeek[weekday(local)-1]);
      
      //Print date in mm/dd/yyyy format
      //datechars starts at 6 because we will always have '/' dividers and 4 year digits
      int datechars = 6; 
      if(month(local) < 10)
      {
        datechars = datechars + 1;
      }
      else
      {
        datechars = datechars + 2;
      }
      if(day(local) < 10)
      {
        datechars = datechars + 1;
      }
      else
      {
        datechars = datechars + 2;
      }
      xpos = 120-((datechars/2)*15);
      if(xpos % 2)
      { 
        //If there is an odd number of characters adjust the position by half a
        //character width.
        xpos = xpos - 5;
      }
      tft.setCursor(xpos, 40);
      tft.setTextColor(numberColor, ILI9340_BLACK);  tft.setTextSize(3);
      tft.print(month(local), DEC);
      tft.print("/");
      tft.print(day(local), DEC);
      tft.print("/");
      tft.print(year(local), DEC);
      colorChange = colorChange - 1;
    }
  }
}


void tftDisplayTime()
{
  //TO-DO: Need to center the time based on the number of digits
  int am_pm_hour;
  if(!rtcSet)
  {
    tft.setCursor(30, 150);
    tft.setTextColor(numberColor, ILI9340_BLACK);  tft.setTextSize(2);
    tft.println("Waiting for GPS Fix");
  }else{
    if(second(local) == 0 or millis() < 2500 or colorChange > 0)
    {
      tft.fillCircle(115, 150, 4, numberColor);
      tft.fillCircle(115, 200, 4, numberColor);
      draw7Number(hourFormat12(local), 0 , 125 , 5 , numberColor, ILI9340_BLACK, -2);
      draw7Number(minute(local), 130 , 125 , 5 , numberColor, ILI9340_BLACK, 2); 
      colorChange = colorChange - 1;
    }

  }
}


void tftDisplayGPSsats()
{
  if(second(local) == 30 or millis() < 2500)
  {
    tft.setCursor(90, 300);
    tft.setTextColor(ILI9340_WHITE, ILI9340_BLACK);  tft.setTextSize(1);
    tft.print("GPS Sats: ");
    tft.print(gps.satellites.value());
    tft.print("  ");
  }
}


void checkRTCset()
{
  DateTime now = rtc.now();
  if(now.hour() == 0 && now.minute() == 0 && now.second() == 0 && now.year() < 2010)
  {
    Serial.println("*** RTC TIME NOT SET. ***");
    Serial.println();
  }else{
    rtcSet = true;
    Serial.println("RTC TIME SET.");
    Serial.println();

  }
}


void syncOnBoot()
{
  if(newboot)
  {
    int satcount = gps.satellites.value();

    if (gps.date.isValid() && gps.time.isValid() && satcount > 4)
    {
      Serial.print(F("New boot. Need to update RTC with GPS time. Sat count: "));
      Serial.println(satcount);
      setRTCfromGPS();
      newboot = false;
      syncTimer = 0;
    }else{
      if(currentMillis > (syncTimer + 250)){
        Serial.print(F("GPS not ready yet. Waiting for fix. Sat count: "));
        Serial.println(satcount);
        syncTimer = currentMillis;
      }
    }
  }
}


void setRTCfromGPS()
{
  /* This line sets the RTC with an explicit date & time, for example to set
   * January 21, 2014 at 3am you would call:
   * rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
   */
  Serial.println("Setting RTC from GPS");
  Serial.println();
  if (gps.date.isValid() && gps.time.isValid())
  {
    rtc.adjust(DateTime(gps.date.year(), gps.date.month(), gps.date.day(), gps.time.hour(), gps.time.minute(), gps.time.second()));
    Serial.println(F("RTC set from GPS"));
  }else{
    Serial.println(F("No GPS fix yet. Can't set RTC yet."));
  }
}


void scheduledSync()
{
  /*
   * Re-syncs the RTC with the time stamp from the GPS data once a day at 2am.
   */
  if(currentMillis > (scheduledTimer + 60000)){  //Only allow RTC sync to happen once every 60 sec
    DateTime now = rtc.now();
    if(minute(local) == 21)
    {
      Serial.println(F("Scheduled RTC sync from GPS Time"));
      setRTCfromGPS();
    }
    scheduledTimer = currentMillis;
  }
}


/**********************************************************************************
 Routine to Draw Large 7-Segment formated number with Arduino TFT Library
    by William Zaggle  (Uses TFT Library DrawLine functions).

   int n - The number to be displayed
   int xLoc = The x location of the upper left corner of the number
   int yLoc = The y location of the upper left corner of the number
   int cSe = The size of the number. Range 1 to 10 uses Large Shaped Segments.
   fC is the foreground color of the number
   bC is the background color of the number (prevents having to clear previous space)
   nD is the number of digit spaces to occupy (must include space for minus sign for numbers < 0)
   nD < 0 Suppresses leading zero
  
   Sample Use: Fill the screen with a 2-digit number suppressing leading zero 

          draw7Number(38,20,40,10,WHITE, BLACK, -2);

**********************************************************************************/
void draw7Number(int n, unsigned int xLoc, unsigned int yLoc, char cS, unsigned int fC, unsigned int bC, char nD) {
  unsigned int num = abs(n), i, s, t, w, col, h, a, b, si = 0, j = 1, d = 0;
  unsigned int S2 = 5 * cS;  // width of horizontal segments   5 times the cS
  unsigned int S3 = 2 * cS;  // thickness of a segment 2 times the cs
  unsigned int S4 = 7 * cS;  // height of vertical segments 7 times the cS
  unsigned int x1 = cS + 1;  // starting x location of horizontal segments
  unsigned int x2 = S3 + S2 + 1; // starting x location of right side segments
  unsigned int y1 = yLoc + x1; // starting y location of top side segments
  unsigned int y3 = yLoc + S3 + S4 + 1; // starting y location of bottom side segments
  unsigned int seg[7][3] = {{x1, yLoc, 1}, {x2, y1, 0}, {x2, y3 + x1, 0}, {x1, (2 * y3) - yLoc, 1}, {0, y3 + x1, 0}, {0, y1, 0}, {x1, y3, 1}}; // actual x,y locations of all 7 segments with direction
  unsigned char nums[12] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x67, 0x00, 0x40};  // segment defintions for all 10 numbers plus blank and minus sign
  unsigned char c, cnt;
  
  c = abs(cS);         // get character size between 1 and 10 ignoring sign
  if (c>10) c= 10;
  if (c<1) c = 1;
  
  cnt = abs(nD);      // get number of digits between 1 and 10 ignoring sign
  if (cnt > 10) cnt = 10;
  if (cnt < 1) cnt = 1; 
  
  xLoc += (cnt-1) * (d = S2 + (3 * S3) + 2);   // set starting x at last digit location
  
  while( cnt > 0) {                   // for cnt number of places
  
    --cnt;
    
    if (num > 9) i = num%10;           //get the last digit 
    else if (!cnt && n<0) i = 11;              //show minus sign if 1st position and negative number
    else if (nD < 0 && !num) i = 10;   //show blanks if remaining number is zero
    else i = num;

    num = num/10;   // trim this digit from the number  
    
    for (j = 0; j < 7; ++j) {          // draw all seven segments
      
      if (nums[i] & (1 << j)) col = fC;  // if segment is On use foreground color
      else col = bC;                    // else use background color

      if (seg[j][2]) {
        
        w = S2;                        // Starting width of segment (side)
        t = seg[j][1] + S3;            // maximum thickness of segment
        h = seg[j][1] + cS;            // half way point thickness of segment
        a = xLoc + seg[j][0] + cS;     // starting x location
        b = seg[j][1];                 // starting y location
        
        while (b < h) {                // until x location = half way
           tft.drawFastHLine(a, b, w, col); //Draw a horizontal segment top 
           a--;                         // move the x position by -1
           b++;                         // move the y position by 1
           w += 2;                      // make the line wider by 2
        }
        
      } else {
        
        w = S4;                        // Starting height of segment (side)
        t = xLoc + seg[j][0] + S3;     // maximum thickness of segment
        h = xLoc + seg[j][0] + cS;     // half way point thickness of segment
        a = seg[j][1] + cS;            // starting y location 
        b = xLoc + seg[j][0];          // starting x location
        
        while (b < h) {                // until x location = half way
          tft.drawFastVLine(b, a, w, col);  // Draw a vertical line right side
          a--;                          //  move the y position by -1
          b++;                          //  move teh x position by 1
          w += 2;                       //  make the line wider by 2
        }
      }
      
      while (b < t) {  //finish drawing horizontal bottom or vertical left side of segment 
        if (seg[j][2]) {
           tft.drawFastHLine(a, b, w, col);  // Draw Horizonal line bottom
        } else {
           tft.drawFastVLine(b, a, w, col);  // Draw Vertical line left side
        }
        b++;        // keep moving the x or y draw position until t 
        a++;        // move the length or height starting position back the other way.
        w -= 2;     // move the length or height back the other way
      }
    }
    
    xLoc -=d;       // move to next digit position
  }
}
