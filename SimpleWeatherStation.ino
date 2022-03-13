#include <rpcWiFi.h>
#include <SPI.h>
#include <DHT.h>
#include <TFT_eSPI.h>
#include "Free_Fonts.h"
#include "RTC_SAMD51.h"
#include "DateTime.h"
#include "config.h"

// Udp Library Class
WiFiClient client;
WiFiUDP udp;
unsigned int localPort = 2390; // local port to listen for UDP packets

//NTP and RTC
byte packetBuffer[ NTP_PACKET_SIZE ]; //buffer to hold incoming and outgoing packets for RTC
unsigned long devicetime;

RTC_SAMD51 rtc; //RTC object
DateTime now; // declare a time object

// for use by the Adafuit RTClib library
char daysOfTheWeek[7][5] = { "Sun", "Mon", "Tue", "Wed", "Thur", "Fri", "Sat" };
char monthsOfTheYear[12][5] = { "Jan.", "Feb.", "Mar.", "Apr.", "May.", "Jun.", "Jul.", "Aug.", "Sep.", 
                                "Oct.", "Nov.", "Dec." };
//type denifination using alias
using arraytype = std::array< float, 2 >;

//declare the Temperature and Humidity object
DHT dht(D0, DHT11);
arraytype temp_humd;
unsigned long lastMesTime {0};

//LCD Display on the WIO
TFT_eSPI tft;
volatile bool SCREEN_FLAG {0};
volatile unsigned long screenTime {0};


//Prototypes for function defined at the bottom of this doucment
void connectWifi( void );
void getTempHumd( void );
void screenShow( void );
void screenOff( void );
void Button_ISR( void );
void Alarm_ISR( uint32_t flag );
unsigned long getNTPtime();

void setup()
{
    //Begin serial
    //Serial.begin(115200);

    //Connect to WiFi
    connectWifi(); 

    // getNTPtime returns epoch UTC time adjusted for timezone but not daylight savings time
    devicetime = getNTPtime();

    // Use Network Time, else use System Time get .
    if (devicetime != 0)
    {
        rtc.begin();
        rtc.adjust(DateTime(devicetime)); //Using the network time to setup rtc 
    }
    else
    {
        Serial.println("Failed to get time from network time server.");
        DateTime systemTime = DateTime(F(__DATE__), F(__TIME__));
        rtc.begin();
        rtc.adjust(systemTime);
    }
 
    //get and print the adjusted rtc time
    now = rtc.now();
    Serial.print("Adjusted RTC (boot) time is: ");
    Serial.println(now.timestamp(DateTime::TIMESTAMP_FULL));

    // Set up alarm to tick time when screen is on
    DateTime alarm = DateTime(now.year(), now.month(), now.day(), now.hour(), now.minute()+1); //every minute
    rtc.setAlarm(0, alarm);
    rtc.enableAlarm(0, rtc.MATCH_SS); //Enable alarm to match every minute
    rtc.attachInterrupt(Alarm_ISR);

    //Start the Temp
    dht.begin(); 

    // Set up display
    tft.begin();
    tft.setRotation(3);
    tft.setTextColor(TFT_BLACK);
    digitalWrite(LCD_BACKLIGHT, HIGH);
    delay(3000);
    digitalWrite(LCD_BACKLIGHT, LOW);
    
    //Setup Interrupt to wake screen
    pinMode(WIO_KEY_C, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(WIO_KEY_C), Button_ISR, FALLING); //Falling is better to avoid bouncing
}

void loop()
{
    // Measure only after every 5mins
    unsigned long now = millis();
    if (now - lastMesTime > 300000)
    {
        lastMesTime = now;

        getTempHumd(); //get temperature and Humidity
    }

    
    //Screen stays on for 3mins
    if (SCREEN_FLAG)
    {
        unsigned long now = millis();
        if (now - screenTime > 180000)
        {
            screenOff();
            SCREEN_FLAG = !SCREEN_FLAG;
            screenTime = 0;
        }  
    }
        
}

//Function to connect WiFi
void connectWifi( void )
{
    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print( "[Status] WiFi: Connecting...\n" );
        WiFi.begin(SSID, Password);
        delay(500);
    }
    Serial.print( "[Status] WiFi: Connected!\n" );
}

//Function to read temperature & Humdity
void getTempHumd( void )
{
    temp_humd[0] = dht.readTemperature();
    temp_humd[1] = dht.readHumidity();
}

//Function to format datetime to string
void DateTimeStringFormatter( DateTime & now, String & date_, String & clock_ )
{
    date_ = String( daysOfTheWeek[ now.dayOfTheWeek() ] ) + ", ";
    date_ += ( String( now.day() ) + " " + String( monthsOfTheYear[ now.month() - 1 ]  ) + 
                " " + String( now.year() ) );
    clock_ = String(now.hour()) + ":";
    clock_ += now.minute() < 10 ? "0" + String(now.minute()) : String(now.minute());
}

//Template for Screen
void screenTemplate (void)
{
  tft.fillScreen(TFT_WHITE);
  tft.fillRect(1, 1, 318, 238, TFT_BLACK);
  tft.fillRect(4, 4, 312, 232, TFT_WHITE);
  tft.fillRect(1, 100, 318, 50, TFT_BLACK);
  tft.fillRect(4, 103, 312, 44, TFT_WHITE);
  tft.fillRect(159, 100, 3, 138, TFT_BLACK);
  tft.drawString("Temperature", 8, 112);
  tft.drawString("Humidity", 190, 112);
}

//Function to show screen and display temp & Humdity
void screenShow( void )
{
    digitalWrite(LCD_BACKLIGHT, HIGH);
    tft.setFreeFont(&FreeSansBoldOblique12pt7b);
    tft.setTextColor(TFT_BLACK);
    screenTemplate();
    DateTime now = rtc.now();
    String devicedate = "";
    String devicetime = "";
    DateTimeStringFormatter( now, devicedate, devicetime  );
    tft.setFreeFont(&FreeSansBoldOblique18pt7b);
    tft.drawString( devicedate,  15, 8);
    tft.setTextColor(TFT_RED);
    tft.setFreeFont(&FreeSansBoldOblique24pt7b);
    tft.drawString(devicetime, 100, 45);
    tft.setFreeFont(&FreeSansBoldOblique18pt7b);
    tft.setTextColor(TFT_BLACK);
    tft.drawString(String(temp_humd[0], 2) + " °C", 15, 180); 
    tft.drawString(String(temp_humd[1], 2) + " %", 175, 180); 
    tft.fillCircle(115,180,4,TFT_BLACK);
    tft.fillCircle(115,180,1,TFT_WHITE); 
}

//Function to clear screen and turn it off
void screenOff( void )
{
    tft.fillScreen(TFT_WHITE);
    digitalWrite(LCD_BACKLIGHT, LOW);
}

//Interrupt Service Routine for button A 
void Button_ISR( void )
{
  Serial.println("Button Pressed!");
  
    if ( !SCREEN_FLAG )
    {
        screenTime = millis();
        screenShow();
        SCREEN_FLAG = !SCREEN_FLAG ;
    }
    else
    {
        screenOff();
        SCREEN_FLAG = !SCREEN_FLAG;
        screenTime = 0;
    }
}

//Interrupt Service Routine (aka Alarm action) for rtc object.
void Alarm_ISR( uint32_t flag )
{
    Serial.println("Alarm Match!");
    if (SCREEN_FLAG)
    {
        Serial.println("Alarm in flag!");
        DateTime now = rtc.now();
        String devicedate = "";
        String devicetime = "";
        tft.setFreeFont(&FreeSansBoldOblique18pt7b);
        tft.setTextColor(TFT_BLACK);
        DateTimeStringFormatter( now, devicedate, devicetime  );
        tft.fillRect(12, 8, 300, 85, TFT_WHITE);
        tft.drawString( devicedate,  15, 8);
        tft.setTextColor(TFT_RED);
        tft.setFreeFont(&FreeSansBoldOblique24pt7b);
        tft.drawString(devicetime, 100, 45);
    }
}

// Function to send an NTP request to the time server at the given address
unsigned long sendNTPpacket(const char* address) 
{
    //set all bytes to zero
    for( int i = 0; i < NTP_PACKET_SIZE; ++i)
    {
        packetBuffer[i] = 0;
    }

    //Initialize value for ntp request
    packetBuffer[0] = 0b11100011 ;   // LI, Version, Mode
    packetBuffer[1] = 0;     // Stratum, or type of clock
    packetBuffer[2] = 6;     // Polling Interval
    packetBuffer[3] = 0xEC;  // Peer Clock Precision
    // 8 bytes of zero for Root Delay & Root Dispersion
    packetBuffer[12] = 49;
    packetBuffer[13] = 0x4E;
    packetBuffer[14] = 49;
    packetBuffer[15] = 52;

    //request timestamp
    udp.beginPacket(address, 123); //port 123 for NTP
    udp.write(packetBuffer, NTP_PACKET_SIZE);
    udp.endPacket();
}

//Function to get NTP time
unsigned long getNTPtime()
{

    //Check if wifi is connected and start udp
    if (WiFi.status() == WL_CONNECTED)
    {
        //start udp
        udp.begin(WiFi.localIP(), localPort);

        //send packet to timeserver and wait for response
        sendNTPpacket(TimeServer);
        delay(1000);

        if (udp.parsePacket()) //If data is recieved
        {
            Serial.println("udp packet received\n");

            udp.read(packetBuffer, NTP_PACKET_SIZE); //read into buffer

            //the timestamp starts at byte 40 of the received packet and is four bytes,
            unsigned long highword = word(packetBuffer[40], packetBuffer[41]);
            unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);

            //combine both words to return the seconds since 1 Jan 1990
            unsigned long sec1900 = highword << 16 | lowWord;

            // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
            const unsigned long seventyYears = 2208988800UL;
            unsigned long epoch = sec1900 - seventyYears;

            //adjusting for time difference, GMT + 1 in central europe (60 * 60 * 24)
            unsigned long tzOffset = 3600UL;

            unsigned long adjustedTime = epoch + tzOffset;
            
            
            return adjustedTime;
            
        }
        else
        {
            udp.stop();
            return 0;

        }

        udp.stop(); //release resources
            
    }
    else
        return 0;

}
