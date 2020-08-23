
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <TimeLib.h>

//remove the following defintion after development
//it is insecure, allows CORS!!
//SEE https://developer.mozilla.org/nl/docs/Web/HTTP/CORS/Errors/CORSMissingAllowOrigin
//#define CORS server.sendHeader("Access-Control-Allow-Origin", "*"); server.sendHeader("Access-Control-Allow-Methods", "GET,HEAD,PUT"); 
#define CORS

#define SERIALINBUFSIZE 3200

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
char version[]="2.1";

void handleRoot();
void handleEsp();
void handleStatus();
void handleSet();
void handleFavicon();
void handleNotFound();

void setup() {
  Serial.begin(115200);
  Serial.print ("WK: start with SSID: ");
  Serial.println(WiFi.SSID());
  //Serial.println(WiFi.psk()); //stored wireless password
  WiFiManager wifiManager;
  wifiManager.autoConnect("WoordklokSetup", "woordklokcharles");
  Serial.print("WK: Connected to ");
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
  server.on("/esp", handleEsp);
  server.on("/set", handleSet);
  server.on("/favicon.ico", handleFavicon);
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

void handleEsp(){
  Serial.println("handleEsp");
  time_t t=now();
  char hh[145];
  sprintf(hh, "{\
\"ver\":\"%s\",\n\
\"ssid\":\"%s\",\n\
\"now\":\"%d-%02d-%02dT%d:%02dZ\",\n\
\"last\": \"%04d-%02d-%02dT%d:%02dZ\",\n\
\"first\": \"%04d-%02d-%02dT%d:%02dZ\",\n\
\"calls\": %d,\n\
\"successes\": %d}", 
    version, WiFi.SSID().c_str(),
    year(t), month(t), day(t), hour(t), minute(t), 
    year(lastNtpTime), month(lastNtpTime), day(lastNtpTime), hour(lastNtpTime), minute(lastNtpTime), 
    year(firstNtpTime), month(firstNtpTime), day(firstNtpTime), hour(firstNtpTime), minute(firstNtpTime), 
    numberOfNtpCalls, numberOfNtpSuccesses);
  CORS;
  server.sendHeader("Expires", "-1");
  server.send(200, "application/json", hh);
}

void handleStatus(){
  //first empty the serial input buffer
  while(Serial.available()>0){
    Serial.read();
  }
  //send status request
  Serial.print("<S>");

  //read the response
  static char buf[SERIALINBUFSIZE+3];
  char c;
  int i=0;
  int t=0;
  //wait for the "{"
  do {
    c=Serial.read();
    if(c==255){
      delay(10);
      t++;
    }
  }while((c!='{')&&(t<400));
  buf[0]=c;
  i++;

  //read until the "}"
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
  }while((c!='}')&&(i<SERIALINBUFSIZE)&&(t<4000));
  CORS;
  server.sendHeader("Expires", "-1");
  if(i>1){
    buf[i]=0; 
    server.send(200, "text/plain", buf);
  }else{
    server.send(504, "text/plain", "Klok antwoordt niet");
  }
}

void handleSet(){
  CORS;
  if(server.args()==1){
    Serial.print("<P ");  
    Serial.print(server.argName(0));
    Serial.print("=");  
    Serial.print(server.arg(0));
    Serial.println(">");
    server.send(200, "text/plain", "Commando naar klok gestuurd");
  }else{
    server.send(400, "text/plain", "Een (1) query parameter nodig (/set?x=y)");
  }
}

void handleFavicon(){
  Serial.println("handleFavicon");
  CORS;
  server.send(200, "application/json", "");
}

void handleNotFound(){
  Serial.print("handleNotFound ");
  Serial.println(server.uri());
  CORS;
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
    } else {
      UDPNTPClient.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
      unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
      unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
      unsigned long fraction = word(packetBuffer[44], packetBuffer[45]);
      unsigned long secsSince1900 = highWord << 16 | lowWord;
      if(secsSince1900==0){ //fixed cvdo 2020-08-23
          Serial.println("Ntp zero seconds");    //in theory this could mean 2036-02-07T06:28:18 but it's more likely an error and we ignore this ntp packer. Also see: The Year 2036 Problem of NTP
      }else{
          numberOfNtpSuccesses++;
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
  }
  UDPNTPClient.stop();
  //return false;
}

void handleRoot(){
  Serial.println("handleRoot");
  CORS;
  server.send(200, "text/html; charset=UTF-8", 
"<html>"
"<head>"
"<title>Woordklok</title>"
"<script>"
"function loadPage() {"
"  var i;"
"  var z=document.getElementById(\"z\");"
"  for(i=0; i<=25; i++){"
"     var o=document.createElement(\"option\");"
"     o.id=\"z\"+i;"
"     o.value=i;"
"     o.appendChild(document.createTextNode(\"+\"+i+\":00\"));"
"     z.appendChild(o);"
"  }"
"}"
"function loadEsp() {"
"  var xhttp = new XMLHttpRequest();"
"  xhttp.onreadystatechange = function() {"
"    if (this.readyState == 4 && this.status == 200) {"
"     document.getElementById(\"esp\").innerHTML = this.responseText;"
"     var obj=JSON.parse(this.responseText);"
"     document.getElementById(\"espver\").innerHTML = obj.ver;"
"     document.getElementById(\"espssid\").innerHTML = obj.ssid;"
"     document.getElementById(\"espnow\").innerHTML = obj.now;"
"     document.getElementById(\"esplast\").innerHTML = obj.last;"
"     document.getElementById(\"espfirst\").innerHTML = obj.first;"
"     document.getElementById(\"espcalls\").innerHTML = obj.calls;"
"     document.getElementById(\"espsuccesses\").innerHTML = obj.successes;"
"    }"
"  };"
"  xhttp.open(\"GET\", \"esp\", true);"
"  xhttp.send();"
"}"
"function loadAvr() {"
"  var xhttp = new XMLHttpRequest();"
"  xhttp.onreadystatechange = function() {"
"    if (this.readyState == 4 && this.status == 200) {"
"     document.getElementById(\"avr\").innerHTML = this.responseText;"
"     var obj=JSON.parse(this.responseText);"
"     document.getElementById(\"avrver\").innerHTML = obj.ver;"
"     document.getElementById(\"avrti\").innerHTML = obj.ti;"
"     document.getElementById(\"avrip\").innerHTML = obj.ip;"
"     document.getElementById(\"avranistl\").innerHTML = obj.anistl;"
"     document.getElementById(\"avranisp\").innerHTML = obj.anisp;"
"     document.getElementById(\"avrzone\").innerHTML = obj.zone;"
"     document.getElementById(\"avrdst\").innerHTML = obj.dst;"
"     document.getElementById(\"avrdste\").innerHTML = obj.dste;"
"     document.getElementById(\"avrbri\").innerHTML = obj.bri;"
"     document.getElementById(\"avrfive\").innerHTML = obj.five;"
"     document.getElementById(\"stl\"+obj.anistl).selected = true;"
"     document.getElementById(\"five\"+obj.five).selected = true;"
"     document.getElementById(\"dste\"+obj.dste).selected = true;"
"     document.getElementById(\"z\"+obj.zone).selected = true;"
"    document.getElementById(\"rananisp\").value=obj.anisp;"
"   var automatic=(obj.bri==\"a\");"
"     document.getElementById(\"auto\").checked = automatic;"
"  if(!automatic)document.getElementById(\"ranbri\").value=obj.bri;"
"    }"
"  };"
"  xhttp.open(\"GET\", \"status\", true);"
"  xhttp.send();"
"}"
"function selectEvt(event){"
"  console.log(\"selectEvt \" + event.target.id);"
"  var ch=event.target.children;"
"  var i;"
"  for (i = 0; i < ch.length; i++) {"
"    if(ch[i].selected) {"
" console.log(ch[i].value);"
" var xhttp = new XMLHttpRequest();"
" xhttp.open(\"POST\", "
"   \"set?\"+event.target.id+\"=\"+ch[i].value, true);"
" xhttp.send();"
" loadAvr();"
"    }"
"  }"
"}"
"function changeBrightness(){"
"  var v;"
"  if(document.getElementById(\"auto\").checked){"
"    v=\"a\";"
"  }else{"
"    v=document.getElementById(\"ranbri\").value;"
"  }"
"  console.log(\"changeBrightness\"+v);"
"  var xhttp = new XMLHttpRequest();"
"  xhttp.open(\"POST\", \"set?b=\"+v, true);"
"  xhttp.send();"
"  loadAvr();"
"}"
"function changeAnisp(){"
"  console.log(\"changeAnisp\");"
"  var xhttp = new XMLHttpRequest();"
"  xhttp.open(\"POST\", \"set?s=\"+document.getElementById(\"rananisp\").value, true);"
"  xhttp.send();"
"  loadAvr();"
"}"
"</script>"
"</head>"
"<body onload=\"loadPage();loadEsp();loadAvr()\">"
"<h1>Woordklok</h1>"
"<table>"
"<tr><td>Animatiestijl:</td><td id=\"avranistl\"/>"
"<td><select id=\"a\" onchange=\"selectEvt(event)\">"
"<option value=\"0\" id=\"stl0\">Geen animatie</option>"
"<option value=\"1\" id=\"stl1\">Snake</option>"
"<option value=\"2\" id=\"stl2\">Snail</option>"
"<option value=\"3\" id=\"stl3\">Rain</option>"
"</select></td></tr>"
"<tr><td>Animatiesnelheid:</td><td id=\"avranisp\"/>"
"<td><input type=\"range\" id=\"rananisp\" min=\"0\" max=\"250\" onchange=\"changeAnisp()\"/>"
"</td></tr>"
"<tr><td>Tijdzone:</td><td id=\"avrzone\"/>"
"<td><select id=\"z\" onchange=\"selectEvt(event)\">"
"</td></tr>"
"<tr><td>Zomertijd automatisch:</td><td id=\"avrdste\"/>"
"<td><select id=\"d\" onchange=\"selectEvt(event)\">"
"<option value=\"0\" id=\"dste0\">Geen zomertijd</option>"
"<option value=\"1\" id=\"dste1\">Zomertijd automatisch</option>"
"</select></td></tr>"
"<tr><td>Helderheid:</td><td id=\"avrbri\"/>"
"<td><input type=\"range\" id=\"ranbri\" min=\"0\" max=\"230\" onchange=\"changeBrightness()\"/>"
"<input type=\"checkbox\" id=\"auto\" onchange=\"changeBrightness()\">Auto</input>"
"</td></tr>"
"<tr><td>Vijfminutenstijl:</td><td id=\"avrfive\"/>"
"<td><select id=\"f\" onchange=\"selectEvt(event)\">"
"<option value=\"0\" id=\"five0\">Tijdweergave per minuut</option>"
"<option value=\"1\" id=\"five1\">Afgerond naar beneden op vijf minuten</option>"
"<option value=\"2\" id=\"five2\">Afgerond naar dichtstbijzijnde 5 minuten</option>"
"</select></td></tr>"
"</table>"
"<p><input type=\"button\" value=\"Meer\" onclick=\"document.getElementById('more').style.visibility='visible';\"></p>"
"<div id=\"more\" style=\"visibility:hidden\"><table>"
"<tr><td>ESP softwareversie</td><td id=\"espver\"/></tr>"
"<tr><td>SSID</td><td id=\"espssid\"/></tr>"
"<tr><td>ESP tijd (UTC)</td><td id=\"espnow\"/></tr>"
"<tr><td>Laatste NTP aanroep:</td><td id=\"esplast\"/></tr>"
"<tr><td>Eerste NTP aanroep:</td><td id=\"espfirst\"/></tr>"
"<tr><td>Aantal NTP aanroepen:</td><td id=\"espcalls\"/></tr>"
"<tr><td>Succesvolle NTP aanroepen:</td><td id=\"espsuccesses\"/></tr>"
"<tr><td>ATMEGA softwareversie:</td><td id=\"avrver\"/></tr>"
"<tr><td>Tijd in ATMEGA:</td><td id=\"avrti\"/></tr>"
"<tr><td>IP adres in ATMEGA:</td><td id=\"avrip\"/></tr>"
"<tr><td>Zomertijd:</td><td id=\"avrdst\"/></tr>"
"</table><p>ESP: <span id=\"esp\">waiting...</span></p>"
"<p>AVR: <span id=\"avr\">waiting...</span></p>"
"</div>"
"</body>"
"</html>");
}
