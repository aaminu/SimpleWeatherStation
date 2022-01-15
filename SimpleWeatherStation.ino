#include <rpcWiFi.h>
#include <SPI.h>
#include <DHT.h>
#include <TFT_eSPI.h>
#include "RTC_SAMD51.h"
#include "DateTime.h"
#include "config.h"

// Udp Library Class
WiFiClient client;
WiFiUDP udp;
unsigned int localPort = 2390; // local port to listen for UDP packets

//NTP and RTC
byte packetBuffer[ NTP_PACKET_SIZE ]; //buffer to hold incoming and outgoing packets
unsigned long devicetime;

RTC_SAMD51 rtc; //RTC object
DateTime now; // declare a time object

// for use by the Adafuit RTClib library
char daysOfTheWeek[7][12] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

//type denifination using alias
using arraytype = std::array< float, 2 >;

//declare the Temperature and Humidity object
DHT dht(D0, DHT11);
arraytype temp_humd;

//LCD Display on the WIO
TFT_eSPI tft;
volatile bool SCREEN_FLAG {0};
volatile unsigned int X_OFFSET {0};
const unsigned int Y_OFFSET {21};


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
//     //Begin serial
       Serial.begin(115200);
       while (!Serial)
        {
            ;
        }

    //Connect to WiFi
    connectWifi(); 

    // getNTPtime returns epoch UTC time adjusted for timezone but not daylight savings time
    devicetime = getNTPtime();

    // Use Network Time, else use System Time get .
    if (devicetime == 0)
    {
        Serial.println("Failed to get time from network time server.");
        rtc.begin();
        rtc.adjust(DateTime(devicetime)); //Using the network time to setup rtc
    }
    else
    {
        DateTime systemTime = DateTime(F(__DATE__), F(__TIME__));
        rtc.begin();
        rtc.adjust(systemTime); 
    }
 
    //get and print the adjusted rtc time
    now = rtc.now();
    Serial.print("Adjusted RTC (boot) time is: ");
    Serial.println(now.timestamp(DateTime::TIMESTAMP_FULL));

    // Set up alarm to tick time when screen is on
    DateTime alarm = DateTime(now.year(), now.month(), now.day(), now.hour(), now.minute()+1); 
    rtc.setAlarm(0, alarm);
    rtc.enableAlarm(0, rtc.MATCH_SS); //Enable alarm to match every minute
    rtc.attachInterrupt(Alarm_ISR);


    //Start the Temp
    dht.begin(); 


    // Set up display
    tft.begin();
    tft.setRotation(3);
    tft.setTextColor(TFT_BLACK);
    tft.setTextSize(3);
    digitalWrite(LCD_BACKLIGHT, HIGH);
    delay(3000);
    digitalWrite(LCD_BACKLIGHT, LOW);
    

    //Setup Interrupt to wake screen
    pinMode(WIO_KEY_A, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(WIO_KEY_A), Button_ISR, LOW);
}

void loop()
{
    getTempHumd();
    delay(300000);

    //check if screen is on
    if (SCREEN_FLAG)
    {
        screenOff();
        SCREEN_FLAG = !SCREEN_FLAG;
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
void DateTimeStringFormatter( DateTime & now, String & date_time )
{
    date_time = ( String( now.year() ) + "/" + String( now.month() ) + "/" + String( now.day() ) );
    date_time += " ";
    date_time += String( now.hour() );
    date_time += ":";
    date_time += String( now.minute() );
}

//Function to show screen and display temp & Humdity
void screenShow( void )
{
    digitalWrite(LCD_BACKLIGHT, HIGH);
    tft.fillScreen(TFT_WHITE);
    DateTime now = rtc.now();
    String devicedatetime = "";
    DateTimeStringFormatter( now, devicedatetime );
    X_OFFSET = 15 + ( (devicedatetime.length() -1 ) * 19 );
    tft.drawString(devicedatetime, 0, 0);
    tft.drawString("Temp: " + String(temp_humd[0], 2) + " C", 0, 50); //prints strings from (0, 0)
    tft.drawString("Hum: " + String(temp_humd[1], 2) + " %", 0, 100); //prints strings from (0, 0)
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
    if ( !SCREEN_FLAG )
    {
        screenShow();
        SCREEN_FLAG = !SCREEN_FLAG ;
    }
    else
    {
        screenOff();
        SCREEN_FLAG = !SCREEN_FLAG;
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
        String devicedatetime = "";
        DateTimeStringFormatter( now, devicedatetime );
        tft.fillRect( 0, 0, X_OFFSET+5, Y_OFFSET+5, TFT_WHITE );
        tft.drawString(devicedatetime, 5, 5);
        X_OFFSET = 15 + ( (devicedatetime.length() -1 ) * 19 );
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
