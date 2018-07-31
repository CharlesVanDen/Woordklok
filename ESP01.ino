#include <ESP8266WiFi.h>
#include <TimeLib.h>
#include <WiFiUdp.h>

void setup(){
  Serial.begin(115200);

  while(1){
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println();
      Serial.print("Connecting");
      WiFi.begin("yourSSID", "yourWifiPassword");
    }
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println();
    Serial.print("Connected, IP address: ");
    Serial.println(WiFi.localIP());
    
    NTPRefresh();
    delay(10000);
  }
}

void loop() {}

/*
 **
 **  NTP
 **
 */

WiFiUDP UDPNTPClient;                      // NTP Client

const int NTP_PACKET_SIZE = 48;
byte packetBuffer[NTP_PACKET_SIZE];

void  NTPRefresh() {
  UDPNTPClient.begin(2390);  // Port for NTP receive
  String ntpServerName="0.de.pool.ntp.org";

  Serial.println("NTPRefresh");
  Serial.println(ntpServerName);
  if (WiFi.status() == WL_CONNECTED) {
    IPAddress timeServerIP;
    WiFi.hostByName(ntpServerName.c_str(), timeServerIP);

    Serial.println("Sending NTP packet... ");
    memset(packetBuffer, 0, NTP_PACKET_SIZE);
    packetBuffer[0] = 0b11100011;   // LI, Version, Mode
    packetBuffer[1] = 0;     // Stratum, or type of clock
    packetBuffer[2] = 6;     // Polling Interval
    packetBuffer[3] = 0xEC;  // Peer Clock Precision
    packetBuffer[12] = 49;
    packetBuffer[13] = 0x4E;
    packetBuffer[14] = 49;
    packetBuffer[15] = 52;
    UDPNTPClient.beginPacket(timeServerIP, 123);
    UDPNTPClient.write(packetBuffer, NTP_PACKET_SIZE);
    UDPNTPClient.endPacket();

    delay(1000);

    int cb = UDPNTPClient.parsePacket();
    if (!cb) {
      Serial.println("Ntp no packet yet");
    } else {
      //Serial.println("Ntp packet received; length: " + (String) cb);
      UDPNTPClient.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
      unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
      unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
      unsigned long fraction = word(packetBuffer[44], packetBuffer[45]);
      unsigned long secsSince1900 = highWord << 16 | lowWord;
      unsigned int leapIndicator=packetBuffer[0]>>6;
      const unsigned long seventyYears = 2208988800UL;
      unsigned long nu = secsSince1900 - seventyYears;
      unsigned int msToWait=1000-(1000ULL*fraction/0x10000);
      delay(msToWait);
      nu+=2;//because of the delay(1000) earlier and the fraction of seconds
      //nu+=3600;//because of the time zone
      //nu+=3600;//because of DST (if applicable -- tbd)
      setTime(nu);
      UDPNTPClient.flush();
      UDPNTPClient.stop();
      Serial.println("Ntp packet time is: " + (String) nu);
      Serial.println("mstowait is: " + (String) msToWait);
      Serial.println("leapindicator is: " + (String) leapIndicator);

      for(int i=0; i<NTP_PACKET_SIZE; i++){
        Serial.print(packetBuffer[i]);
        Serial.print(",");
        if(i%4==3)Serial.println();
      }
      time_t t=now();

      Serial.print("<T");
      if(hour(t)<10)Serial.print("0");
      Serial.print(hour(t));
      Serial.print(":");
      if(minute(t)<10)Serial.print("0");
      Serial.print(minute(t));
      Serial.print(":");
      if(second(t)<10)Serial.print("0");
      Serial.print(second(t));
      Serial.println(">");

      delay(2000);
      t=now();
      Serial.print("<D");
      Serial.print(year(t));
      Serial.print("-");
      if(month(t)<10)Serial.print("0");
      Serial.print(month(t));
      Serial.print("-");
      if(day(t)<10)Serial.print("0");
      Serial.print(day(t));
      Serial.println(">");
    }
  }
  UDPNTPClient.stop();
  //return false;
}
