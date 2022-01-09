#include <rpcWiFi.h>
#include <SPI.h>
#include <DHT.h>
#include <TFT_eSPI.h>
#include "RTC_SAMD51.h"
#include "DateTime.h"

#include "config.h"

//NTP and RTC
byte packetBuffer[ NTP_PACKET_SIZE ];
unsigned long devicetime;
RTC_SAMD51 rtc;

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


void setup()
{
//     //Begin serial
       Serial.begin(115200);
       while (!Serial)
        {
            ;
        }

    //Connect to WiFi
    //connectWifi(); 

    //rtc 
    rtc.begin();
    DateTime now = DateTime(F(__DATE__), F(__TIME__));
    rtc.adjust(now);
    now = rtc.now();
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
    

    //Setup Interrupt
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
    if (WiFi.status() != WL_CONNECTED)
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

//Interrupt Service Routine
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


void Alarm_ISR( uint32_t flag )
{
    Serial.println("Alarm Match!");
    if (SCREEN_FLAG)
    {
        Serial.println("Alarm in flag!");

        tft.fillRect( 0, 0, X_OFFSET+5, Y_OFFSET+5, TFT_WHITE );
        DateTime now = rtc.now();
        String devicedatetime = "";
        DateTimeStringFormatter( now, devicedatetime );
        tft.drawString(devicedatetime, 5, 5);
        X_OFFSET = 15 + ( (devicedatetime.length() -1 ) * 19 );
    }
}
