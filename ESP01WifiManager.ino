
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <TimeLib.h>

ESP8266WebServer server(80);    // Create a webserver object that listens for HTTP request on port 80
WiFiUDP UDPNTPClient;  
MDNSResponder mdns; //create a mdns domain name woordklok.local
// NTP Client
const int NTP_PACKET_SIZE = 48;
byte packetBuffer[NTP_PACKET_SIZE];
uint16_t numberOfNtpCalls=0;
uint16_t numberOfNtpSuccesses=0;
time_t lastNtpTime=0;
time_t firstNtpTime=0;

void handleRoot();
void handleStatus();
void handleSet();
void handleNotFound();

void setup() {
  Serial.begin(115200);
  WiFiManager wifiManager;
  wifiManager.autoConnect("WoordklokSetup", "woordklokcharles");
  Serial.print("Connected to ");
  Serial.println(WiFi.SSID());
  Serial.printf("Hostname: %s\n", WiFi.hostname().c_str());
  IPAddress ipa=WiFi.localIP();
  delay(10);
  Serial.print("<I");
  for(uint8_t i=0; i<4; i++){
    Serial.printf("%02X",ipa[i]);
    delay(1);
  }
  Serial.println(">");
  MDNS.begin("woordklok");
  MDNS.addService("http", "tcp", 80);
  
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/set", handleSet);
  server.onNotFound(handleNotFound);        // When a client requests an unknown URI
  server.begin();                           // Actually start the server
  Serial.println("HTTP server started");
}

void loop() {
  static int net=99;
  int s=minute(now())/10;
  if((s!=net)||(year(now())==1970)){
    NTPRefresh();
  }
  server.handleClient(); 
  net=s;
}

void handleRoot(){
  Serial.println("handleRoot");
  time_t t=now();
  char hh[145];
  sprintf(hh, "It is now: %d-%02d-%02dT%d:%02dZ\nLast NTP time: %04d-%02d-%02dT%d:%02dZ\nFirst NTP time: %04d-%02d-%02dT%d:%02dZ\nNumber of NTP calls: %d\nNumber of NTP successes: %d", 
    year(t), month(t), day(t), hour(t), minute(t), 
    year(lastNtpTime), month(lastNtpTime), day(lastNtpTime), hour(lastNtpTime), minute(lastNtpTime), 
    year(firstNtpTime), month(firstNtpTime), day(firstNtpTime), hour(firstNtpTime), minute(firstNtpTime), 
    numberOfNtpCalls, numberOfNtpSuccesses);
  server.send(200, "text/plain", hh);
}

void handleStatus(){
  //first empty the serial input buffer
  while(Serial.available()>0){
    Serial.read();
  }
  //send status request
  Serial.print("<S>");

  //read the response
  char buf[83];
  char c;
  int i=0;
  int t=0;
  do {
    c=Serial.read();
    if(c!=255){
      buf[i]=c;
      i++;
    }else {
      //Serial.print(".");
      delay(10);
      t++;
    }
  }while((c!='/')&&(i<80)&&(t<200));
  if(i>0){
    buf[i++]='S'; 
    buf[i++]='>'; 
    buf[i]=0; 
    server.send(200, "text/plain", buf);
  }else{
    server.send(504, "text/plain", "Klok antwoordt niet");
  }
}

void handleSet(){
  if(server.args()==1){
    Serial.print("<P>");  
    Serial.print(server.argName(0));
    Serial.print("=");  
    Serial.print(server.arg(0));
    Serial.println("</P>");
    server.send(200, "text/plain", "Commando naar klok gestuurd");
  }else{
    server.send(400, "text/plain", "Een (1) query parameter nodig (/set?x=y)");
  }
}

void handleNotFound(){
  Serial.println("handleNotFound");
  server.send(404, "text/plain", "404: Not Found!");
}

void  NTPRefresh() {
  UDPNTPClient.begin(2390);  // Port for NTP receive
  String ntpServerName="0.de.pool.ntp.org";

  Serial.println("NTPRefresh");
  Serial.println(ntpServerName);
  if (WiFi.status() == WL_CONNECTED) {
    IPAddress timeServerIP;
    WiFi.hostByName(ntpServerName.c_str(), timeServerIP);
    numberOfNtpCalls++;
    Serial.println("Zending NTP packet... ");
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
      server.send(200, "text/plain", "No NTP packet received!");
    } else {
      //Serial.println("Ntp packet received; length: " + (String) cb);
      numberOfNtpSuccesses++;
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
      lastNtpTime=nu;
      if(firstNtpTime==0)firstNtpTime=nu;
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

      delay(100);
      t=now();
      Serial.print("<D");
      Serial.print(year(t));
      Serial.print("-");
      if(month(t)<10)Serial.print("0");
      Serial.print(month(t));
      delay(1);//for some reason, the transmission of the date sometimes went wrong, so introduced a little delay
      Serial.print("-");
      if(day(t)<10)Serial.print("0");
      Serial.print(day(t));
      Serial.println(">");
    }
  }
  UDPNTPClient.stop();
  //return false;
}
