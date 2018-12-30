/*
 * WoordKlokCharles.c
 *
 * Created: 6-4-2018 14:10:04
 * Version: 9 JULY 2018, bug in het woord EEN gecorrigeerd
 *  Author: Charles van den Ouweland
 Alternatief voor de originele controller voor de woordklok van samenkopen,
 gebaseerd op een AVR controller i.c.m. een ESP01 module om de tijd op te halen via wifi
 Met een printplaat met geheel through hole componenten.
 
 ATMEGA88PA <-- check uctrler type before compiling!
 16MHz crystal
 
 connect:
 ATMEGA88 pin       MBI5026 pin (rows/cathodes of the leds)
 MOSI PB3 pin 17 to SDI pin 2
 SCK  PB5 pin 19 to CLK pin 3
 SS   PB2 pin 16 to LE  pin 4
      PB1 pin 15 to OE  pin 21
 ATMEGA88 pin       74HC595 pin (and then to KID65783 to the columns/anodes of leds)
 PD5                SDI pin 14
 PD6                CLK pin 11
 PD7                RCK pin 12

The MBI5026 is attached to the SPI pins of the ATMEGA, whereas the 74HC595's are done by software.
This is not a big deal as we clock one 1 into the 74HC595 and then shift that 1 through the shift register just 1 clock pulse at a time.
 
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <stdlib.h>

#define SOFTWARE_VERSION "2.0"

#define UREN_OFFSET 16
#define HET 0
#define IS 29
#define VOOR 30
#define OVER 31
#define HALF 32
#define UUR 33
#define SEC00_14 34
#define SEC15_29 35
#define SEC30_44 36
#define SEC45_59 37
#define NUL_EXTRA 38
#define T_FOR_TWEE 39
#define I_FOR_IP 42
#define A_FOR_ADRES 44
#define EVEN 47
#define NA 48

#define EEPROM_ANIM_SPEED 0
#define EEPROM_ANIM_STYLE 1
#define EEPROM_DST_ENABLED 2
#define EEPROM_TIMEZONE 3
#define EEPROM_BRIGHTNESS 4
#define EEPROM_FIVEMINSTYLE 5

#define NUM_COLUMNS 13
#define KLKDBG 0
#define pixel16_t volatile uint16_t
#define BUFFYSIZE 40
#define LEUK 1

void displayTime();//show the current time on the wordclock
void displayNumber(uint8_t n); //show a number (0...254) on the wordclock
void display_text_ipadress(); //show the text "ip adres" on the woordklok

void printchar(char c);
void print(char*s);
void printint2(uint8_t n);
void printuint(uint8_t n);
uint8_t calcDST();
void checkSerialinput(char k);
void checkLdr();
void setlin(uint8_t index,uint8_t value, pixel16_t*matrix);
uint8_t getlin(uint8_t index, pixel16_t*matrix);


/* LAYOUT DER KLOCK

HETDISAEENANO
TWEEDRIEJVIER
VIJFZESZEVENE
ACHTROENEGENQ
TIENELFTWAALF
KWARTZDERTIEN
VEERTIENYVOOR
OVERXHALFBEEN
TWEEUDRIEVIER
VIJFZESPZEVEN
ACHTNEGENTIEN
ELFTWAALFQUUR

*/

static const uint8_t coordinaten[]={ //ROW, COLUMN, LENGTH
	1,0,3, //HET 0
	1,7,3, //EEN minuten 1
	2,0,4, //TWEE 2
	2,4,4, //DRIE 3
	2,9,4, //VIER 4
	3,0,4, //VIJF 5
	3,4,3, //ZES 6
	3,7,5, //ZEVEN 7
	4,0,4, //ACHT 8
	4,7,5, //NEGEN 9
	5,0,4, //TIEN 10
	5,4,3, //ELF 11
	5,7,6, //TWAALF 12
	6,6,7, //DERTIEN 13
	7,0,8, //VEERTIEN 14
	6,0,5, //KWART 15
	7,10,1, //NUL 16
	8,10,3,//EEN (uren) 17
	9,0,4, //TWEE 18
	9,5,4, //DRIE 19
	9,9,4, //VIER 20
	10,0,4,//VIJF 21
	10,4,3,//ZES 22
	10,8,5,//ZEVEN 23
	11,0,4,//ACHT 24
	11,4,5,//NEGEN 25
	11,9,4,//TIEN 26
	12,0,3,//ELF 27
	12,3,6,//TWAALF 28
	1,4,2, //IS 29
	7,9,4, //VOOR 30
	8,0,4, //OVER 31
	8,5,4, //HALF 32
	12,10,3,//UUR 33
	0,12,1, //SEC00_14 34 de vier hoekleds zitten op rij 0
	0,11,1, //SEC15_29 35
	0,1,1,  //SEC30_44 36
	0,0,1,  //SEC45_59 37
	7,11,1, //NUL_EXTRA 38
	5,0,1,  //T_FOR_TWEE 39
	6,1,1, //W_FOR_TWEE 40
	7,1,2, //EE_FOR_TWEE 41
	9,7,1, //I_FOR_IP 42
	10,7,1,//P_FOR_IP 43
	1,6,1, //A_FOR_ADRES 44
	2,4,2, //DR_FOR_ADRES 45
	3,5,2, //ES_FOR_ADRES 46
	3,8,4, //EVEN 47
	1,9,2, //NA 48
	0
};

volatile uint8_t jaar=0;//alleen de laatste twee cijfers van het jaar.
volatile uint8_t maand=0;
volatile uint8_t dag=0;
volatile uint8_t uur=0;
volatile uint8_t min=0;
volatile uint8_t sec=0;
volatile uint8_t tic=0;
static uint8_t timezone; //lokale tijdzone: 1 uur verschil met UTC
volatile uint8_t dst=0; //daglight saving time (zomertijd)
uint8_t lastSundayInOctober;//end of DST
uint8_t lastSundayInMarch;//start of DST
uint8_t displayMode=0; //mode: 0=display time; 1=display ip address 
uint8_t displayIpIndex=0; //when displaying the ip adres: which digit?
uint8_t ipadress[4];
uint8_t anim_speed;
uint16_t anim_delay; //determines the speed of the animation
uint8_t anim_style; //0=no animation, 1=snake animation, 2=snail animation
uint8_t dst_enabled;//zomertijd or daylight saving time enabled
uint8_t five_min_style; //0: change time display every minute; 1: round down to five minutes, 2: round to nearest 5 min

//variables for the LDR
uint16_t sumLdrSamples=0;
uint8_t numLdrSamples=0;
uint8_t brightnessFixed=0;

uint8_t bufindex=255;
char buf[13];
volatile char buffy[BUFFYSIZE]; //input buffer for serial io
volatile uint8_t buffyIndex=0;//first empty byte

static pixel16_t mypixels[NUM_COLUMNS];//prepare the pixels in this array first then later copy everything to pixels[]
static pixel16_t pixels[NUM_COLUMNS];//each uint16_t stores 1 column of pixels

void delay_ms(uint16_t x){
	uint8_t y, z;
	for ( ; x > 0 ; x--){
		for ( y = 0 ; y < 90 ; y++){
			for ( z = 0 ; z < 6 ; z++){
				asm volatile ("nop");
			}
		}
	}
}

int main(void){
	DDRB=0b00101110;//Set PB1 (OE), PB2 (latch), 3 (MOSI) and 5 (CLK) as output (rows of leds)
	DDRD=0b11100110;//set PD1 (TXD), 5 (data), 6 (clock) and 7 (latch) as output (columns of leds) and PD2 LED for debugging
	SPCR=(1<<SPE)|(1<<MSTR)|(1<<SPR0)|(1<<DORD);//Fspi=fcpu/128 |(1<<DORD) to send LSb first
	
	//setup 8-bit timer/counter 0 in CTC mode (mode 2), F=fcpu/64, 
	/*
	TCCR0A=(1<<WGM01);
	TCCR0B=(1<<CS00)|(1<<CS01);
	OCR0A=249;
	TIMSK0=(1<<OCIE0A);
	*/
	
	//setup 16 bit timer/counter 1 in fast PWM mode (mode 14), F=fcpu/64, generates interrupt every ms, for refreshing one column of the screen every 1ms
	//this timer is also used to generate a PWM waveform on OC1A, which dims the display through the OE pin of the MBI5026
	TCCR1A=(1<<WGM11)|(1<<COM1A1);
	TCCR1B=(1<<WGM12)|(1<<WGM13)|(1<<CS10)|(1<<CS11);
	TCCR1C=0;
	ICR1=249;
	//OCR1A=224;//at 10% brightness
	//OCR1A=124;//at 50% brightness
	//OCR1A=24; //at 90% brightness, this is about maximum otherwise ghosting will appear
	TIMSK1=(1<<TOIE1);

	//setup 8 bit timer/counter 2 in CTC mode (mode 2), F=fcpu/1024, for the real time clock, generates interrupt every 8ms
	TCCR2A=(1<<WGM21);
	TCCR2B=(1<<CS20)|(1<<CS21)|(1<<CS22);
	OCR2A=124;
	TIMSK2=(1<<OCIE2A);

	sei();//enable interrupts

	//setup UART AT 115k2b/s for communication with the ESP01 (which communicates with wifi, internet and ntp time server)
	UCSR0A=(1<<U2X0); //double speed
	UCSR0B=(1<<RXEN0)|(1<<TXEN0)|(1<<RXCIE0);//enable tx & rx & enable interrupt on receive
	//UCSR0B=(1<<RXEN0)|(1<<TXEN0);//enable tx & rx
	UCSR0C=(1<<UCSZ00)|(1<<UCSZ01)|(1<<USBS0);//8 bit, 2 stop bits
	UBRR0=16;//set speed at 117k6 b/s, which is 2% high of 115k2
	print("Klok");
	
	
	//setup the ADC at 140kHz (16MHz/128), input is ADC0=PC0, reference is 1.1V
	//it is used for the Light Dependent Resistor (LDR) to determine ambient light level
	//when there is bright light, the LDR will have a low value and the voltage on ADC0 will be low (say, 0.4V)
	//when there is darkness, the LDR will have a high value and the voltage on ADC0 will be high (1.1V max)
	ADCSRA=(1<<ADEN)|(1<<ADPS0)|(1<<ADPS1)|(1<<ADPS2);
	ADMUX=(1<<REFS0)|(1<<REFS1);
	ADCSRA|=(1<<ADSC);//start conversion
	
	//get settings from eeprom
	anim_speed=eeprom_read_byte((uint8_t*)EEPROM_ANIM_SPEED);
	if(anim_speed==255) anim_speed=165;
	anim_delay=255-anim_speed;
	anim_delay*=anim_delay;
	
	anim_style=eeprom_read_byte((uint8_t*)EEPROM_ANIM_STYLE);
	if(anim_style>2)anim_style=1;
	
	if(eeprom_read_byte((uint8_t*)EEPROM_DST_ENABLED))dst_enabled=1;
	
	timezone=eeprom_read_byte((uint8_t*)EEPROM_TIMEZONE);
	if(timezone>25)timezone=1;
	
	uint8_t b=eeprom_read_byte((uint8_t*)EEPROM_BRIGHTNESS);
	if(b!=255){
		brightnessFixed=1;
		OCR1A=255-b;
	}
	
	five_min_style=eeprom_read_byte((uint8_t*)EEPROM_FIVEMINSTYLE);
	if(five_min_style>2)five_min_style=0;

	for(uint8_t k=0; k<NUM_COLUMNS; k++) pixels[k]=0;

    while(1){
		if(displayMode==0)displayTime();
		else{
			uint8_t what=tic-displayIpIndex;
			what/=2;
			if(what<2)display_text_ipadress();
			else displayNumber(ipadress[what-2]);
			if(what==6)displayMode=0;
		}
		static uint8_t readidx=0;
		if(readidx!=buffyIndex){
			checkSerialinput(buffy[readidx]);
			readidx++;
			cli();
			if(readidx==buffyIndex){
				//all characters processed, reset both indexes to 0
				readidx=0;
				buffyIndex=0;
			}
			sei();
		}
		if(!brightnessFixed)checkLdr();
	}
}

void checkSerialinput(char k){
	//check serial port input. We expect "<T22:23:24> or "<D2017-12-31>"
	if(k=='<'){
		bufindex=0;
		printchar(k);
		
	}else if (bufindex!=255){
		if(k=='>'){
			buf[bufindex++]=k;
			buf[bufindex]=0;
			//now do something with the string received!
			//echo it
			print(buf);
			if(buf[0]=='T'){
				//do syntax check: should be like T22:23:24>
				//Let op: dit is UTC!
				if ((buf[1]>'2')||(buf[1]<'0')||(buf[2]>'9')||(buf[2]<'0')||(buf[3]!=':')||
					(buf[4]>'5')||(buf[4]<'0')||(buf[5]>'9')||(buf[5]<'0')||(buf[6]!=':')||
					(buf[7]>'5')||(buf[7]<'0')||(buf[8]>'9')||(buf[8]<'0')||(buf[9]!='>')||
					((buf[1]=='2')&&(buf[2]>'3'))){
						//syntax error!
						printchar('E');
				}else{
					//syntax is ok
					printchar('K');
					cli();//temporarily disable interrupts
					//set the time
					uur=10*(buf[1]-'0') + buf[2]-'0'; //waarden: 0..23
					min=10*(buf[4]-'0') + buf[5]-'0';
					sec=10*(buf[7]-'0') + buf[8]-'0';
					sei();
				}
			} else if(buf[0]=='D'){
				//do syntax check: should be like D2017-02-31>
				if ((buf[1]!='2')||(buf[2]!='0')||(buf[3]>'9')||(buf[3]<'0')||(buf[4]>'9')||(buf[4]<'0')||(buf[5]!='-')||
				(buf[6]>'1')||(buf[6]<'0')||(buf[7]>'9')||(buf[7]<'0')||(buf[8]!='-')||
				(buf[9]>'3')||(buf[9]<'0')||(buf[10]>'9')||(buf[10]<'0')||(buf[11]!='>')||
				((buf[6]=='1')&&(buf[7]>'2'))||
				((buf[9]=='3')&&(buf[10]>'1'))){
					//syntax error!
					printchar('E');
				}else{
					//syntax is ok
					printchar('K');
					//set the date
					jaar=10*(buf[3]-'0') + (buf[4]-'0');
					maand=10*(buf[6]-'0') + (buf[7]-'0');
					dag=10*(buf[9]-'0') + (buf[10]-'0');
							
				}
			} else if(buf[0]=='I'){
				//DO SYNTAX CHECK: should be like IC0A80068>
				uint8_t ok= (buf[9]=='>');
				for(uint8_t i=1; i<=8; i++){
					if(buf[i]>='A' && buf[i]<='F') buf[i]+=10-'A'; else buf[i]-='0';
					if(buf[i]>0xf) ok=0;
				}
				if(ok){
					//syntax is ok
					printchar('K');
					uint8_t j=1;
					uint8_t changed=0;
					for(uint8_t i=0; i<4; i++) {
						uint8_t b=16*buf[j++];
						b+=buf[j++];
						if(b!=ipadress[i]){
							changed=1;
							ipadress[i]=b;
						}
					}
					if(changed){
						displayMode=1;
						displayIpIndex=tic;
					}
				}else printchar('E');
			} else if(buf[0]=='S'){
				//status request, answer with status
				print("{\"ver\":\"");
				print(SOFTWARE_VERSION);
				print("\",\"ti\":\"");
				printint2(20);
				printint2(jaar);
				printchar('-');
				printint2(maand);
				printchar('-');
				printint2(dag);
				printchar('T');
				printint2(uur+timezone+dst);
				printchar(':');
				printint2(min);
				printchar(':');
				printint2(sec);
				printchar('+');
				printint2(timezone+dst);
				printint2(0);
				print("\",\"ip\":\"");
				printuint(ipadress[0]);
				printchar('.');
				printuint(ipadress[1]);
				printchar('.');
				printuint(ipadress[2]);
				printchar('.');
				printuint(ipadress[3]);
				print("\",\"anistl\":");
				printuint(anim_style);
				print(",\"anisp\":");
				printuint(anim_speed);
				print(",\"zone\":");
				printuint(timezone);
				print(",\"dst\":");
				printuint(dst);
				print(",\"dste\":");
				printuint(dst_enabled);
				print(",\"bri\":");
				if(brightnessFixed)printuint(255-OCR1A);
				else print("\"a\"");
				print(",\"five\":");
				printuint(five_min_style);
				print("}</S>");
			} else if(buf[0]=='P'){
				//this is the PUT command, to set parameters in the clock.
				//general syntax: <P key=value>
				
				//try to interpret value as a decimal number
				uint8_t i=4;
				uint8_t value=0;
				while(buf[i]>='0' && buf[i]<='9') value=value*10+buf[i++]-'0';
				
				if(buf[2]=='a' && buf[4]>='0' && buf[4]<='3'){
					//set animation style: <P a=2>
					anim_style=buf[4]-'0';
					eeprom_update_byte((uint8_t*)EEPROM_ANIM_STYLE, anim_style);
				}
				if(buf[2]=='b'){//brightness: <P b=0> (light off) <P b=7> (minimum visible in the dark) <P b=230> (maximum without ghosting effect) or <P b=auto>
					brightnessFixed= buf[4]!='a';
					if(brightnessFixed) {
						OCR1A=255-value;
						eeprom_update_byte((uint8_t*)EEPROM_BRIGHTNESS, value);
					}else{
						eeprom_update_byte((uint8_t*)EEPROM_BRIGHTNESS, 255);
					}
				}
				if(buf[2]=='d'){//<P d=1> sets dst (zomertijd) enabled
					dst_enabled=(buf[4]!='0');
					eeprom_update_byte((uint8_t*)EEPROM_DST_ENABLED, dst_enabled);
				}
				if(buf[2]=='s'){//<P s=99> sets animation speed. The higher the number the faster
					eeprom_update_byte((uint8_t*)EEPROM_ANIM_SPEED, value);
					anim_speed=value;
					value=255-value;
					anim_delay= value*value;
				}
				if(buf[2]=='z'){//<P z=1> sets timezone
					timezone=value;
					eeprom_update_byte((uint8_t*)EEPROM_TIMEZONE, timezone);
				}
				if(buf[2]=='f' && value<=2){//<P f=1> sets five minutes style
					five_min_style=value;
					eeprom_update_byte((uint8_t*)EEPROM_FIVEMINSTYLE, five_min_style);
				}
			}
			bufindex=255;
		}else{
			if(bufindex<11){
				buf[bufindex++]=k;
			}else{
				//error: ">" not received in time!
				bufindex=255;
				//print(buf);
				//printchar('E');
			}
		}
	}
}

void checkLdr(){
	if(!(ADCSRA&(1<<ADSC))){//ADC conversion complete
		numLdrSamples++;
		sumLdrSamples+=ADC;
		ADCSRA|=(1<<ADSC);//start new conversion
		if(numLdrSamples==10){
			//uint16_t ldr=sumLdrSamples/30;
			uint16_t ldr=sumLdrSamples/29;
			//if(ldr<141)ldr=141;
			if(ldr<130)ldr=130;
			ldr-=117;
			OCR1A=ldr;
			numLdrSamples=0;
			sumLdrSamples=0;
			/*
			Voorschakelweerstand van 120k Ohm naar 3.3V
			light  | LDR | U(V) | ADC | sumLdr | PWM
			-------+-----+------+-----+--------+-----
			bright | 16k | 0.39 | 363 | 3630   |  24
			medium | 40k | 0.825| 768 | 7680   | 139
			dark   | 60k | 1.1  |1023 |10230   | 224
			*/
		}
	}
}

void setwoord(uint8_t woord, pixel16_t*matrix){
	uint8_t index=3*woord;
	uint8_t row= coordinaten[index];index++;
	uint8_t column= coordinaten[index];index++;
	uint8_t length= coordinaten[index];
	uint16_t pattern= 1<<(row+3);
	uint8_t col=column;
	while(length>0){
		matrix[col++] |= pattern;
		length--;
	}
}

void displayTime(){
	static uint8_t prevsec=0;
	uint8_t newsec=sec;
	if(newsec!=prevsec){
		prevsec=newsec;
		for (uint8_t i=0; i<NUM_COLUMNS; i++) mypixels[i]=0;
		
		setwoord(HET, mypixels);
		setwoord(IS, mypixels);
	
		if(KLKDBG){
			printchar(10);
			printchar(13);
			printint2(20);
			printint2(jaar);
			printchar('-');
			printint2(maand);
			printchar('-');
			printint2(dag);
			printchar('T');
			printint2(uur);
			printchar(':');
			printint2(min);
			printchar(':');
			printint2(sec);
			printchar('+');
			printint2(timezone+dst);
			printint2(0);
			printchar(' ');
			print("HET IS ");
		}
		dst=calcDST();
		uint8_t urenweergave=uur+timezone+dst;
		uint8_t min_afgerond;
		int8_t min_afronding;
		switch(five_min_style){
			case 1: min_afgerond=(min/5)*5; break;
			case 2: min_afgerond=((min+2)/5)*5; break;
			default: min_afgerond=min; break;
		}
		min_afronding=min-min_afgerond;
		if(min_afgerond==60){
			min_afgerond=0;
			urenweergave++;
		}
		if(min_afgerond==0){
			setwoord(UUR, mypixels);
			if(KLKDBG)print("UUR ");
		}
		else{
			uint8_t minutenweergave=min_afgerond%30;
			if(minutenweergave!=0){
				if(minutenweergave>15) minutenweergave=30-minutenweergave;
				setwoord(minutenweergave, mypixels);
				if(KLKDBG){
					printchar('0'+(minutenweergave/10));
					printchar('0'+(minutenweergave%10));
					printchar(' ');
				}
			}
			if(minutenweergave!=0){
				if((min_afgerond<=15)||((min_afgerond>30)&&(min_afgerond<45))){
					setwoord(OVER, mypixels);
					if(KLKDBG)print("OVER ");
				}else{
					setwoord(VOOR, mypixels);
					if(KLKDBG)print("VOOR ");
				}
			}
			if((min_afgerond>15)&&(min_afgerond<45)){
				setwoord(HALF, mypixels);
				if(KLKDBG)print("HALF ");
			}
		}
		if(min_afgerond>15)urenweergave++;
		urenweergave=(urenweergave+11)%12+1;
		setwoord(UREN_OFFSET+urenweergave, mypixels);
		if(KLKDBG)printint2(urenweergave);

		//De vier 15seconden-LED's, zet zowel direct in pixels als in mypixels
		pixels[0]&=~(1<<3);
		pixels[1]&=~(1<<3);
		pixels[11]&=~(1<<3);
		pixels[12]&=~(1<<3);
		if(five_min_style!=0){
			if(min_afronding<0){
				setwoord(SEC45_59, mypixels);
				setwoord(SEC45_59, pixels);
				//een en twee minuten voor het hele en halve uur, toon ook de tekst "even voor"
				if(LEUK && min_afgerond%30==0){
					setwoord(EVEN,mypixels);
					setwoord(VOOR,mypixels);
				}
			}
			if(min_afronding==-2){
				setwoord(SEC30_44, mypixels);
				setwoord(SEC30_44, pixels);
			}
			if(LEUK && min_afronding>0 && min_afgerond%30==0){
				setwoord(NA, mypixels);
			}
			while(min_afronding>0){
				min_afronding--;
				setwoord(min_afronding+SEC00_14, mypixels);
				setwoord(min_afronding+SEC00_14, pixels);
			}
		}else{
			setwoord((sec/15)+SEC00_14, mypixels);
			setwoord((sec/15)+SEC00_14, pixels);
		}
		if(KLKDBG){
			printchar('s');
			printchar('0'+(sec/15));
		}
	}
	if(anim_style==0){
		for(uint8_t i=0; i<NUM_COLUMNS; i++){
			pixels[i]=mypixels[i];
		}
	}else{
		static uint16_t slow=0;
		slow++;
		if(slow==anim_delay){
			slow=0;
			if(anim_style==3){//rain style
				//uint8_t col=rand()%NUM_COLUMNS;
				for(uint8_t col=0; col<NUM_COLUMNS; col++){
					//zoek eerste verschil in deze kolom
					uint16_t ist=pixels[col];
					uint16_t soll=mypixels[col];
					uint8_t row=4;
					uint16_t diff=(ist ^ soll)>>row;
					while((diff&1)==0 && row<16){diff=diff>>1;row++;}
					if(row!=16){//verschil gevonden in row 
						if((soll>>row)&1){//er moet een led aangezet worden op row
							//injecteer een brandende pixel van bovenaf
							//doe dit niet altijd, zodat een random effect ontstaat
							if(rand()%5==0){
								//zoek eerste lege pixel
								row=4;
								ist=ist>>row;
								while((ist&1)==1 && row<16){ist=ist>>1;row++;}
								pixels[col]|=(1<<row);
							}
						}else{//er moet een led uitgezet worden op row
							//zoek eerste lege pixel daaronder
							ist=ist>>row;
							while((ist&1)==1 && row<16){ist=ist>>1;row++;}
							//row is nu de eerste niet brandende
							if(row<16)pixels[col]|=(1<<row);
							row--;//row is nu de laatste brandende
							pixels[col]&=~(1<<row);
						}
					}
				}
			}else{//snail and snake style
				//vergelijk mypixels (de gewenste weergave) met pixels (de huidige weergave). Doe dit pixel voor pixel. Vind je een verschil, verschuif dan een beetje zodat
				//pixels iets beter gaat lijken op mypixels
				//zoek het eerste verschil
				uint8_t i;
				for(i=0;i<12*13; i++){
					if (getlin(i,pixels)!=getlin(i,mypixels)) break;
				}
				if(i<12*13){//zijn er uberhaupt verschillen?
					if(getlin(i,mypixels)){//schuif een 1 naar links
						uint8_t j=i;
						while((getlin(j,pixels)==0) && j<12*13)j++;//zoek de 1
						if(j<12*13){
							setlin(j,0,pixels);
							uint8_t row=j/13;
							uint8_t l=26*row-j;
							//if(j-i>=25 && anim_style==1){//doe een verschuiving omhoog ipv opzij
							if(row>0 && l>i && anim_style==1){//doe een verschuiving omhoog ipv opzij
								j=l;
							}
						}
						setlin(j-1,1,pixels);
					}else{//schuif een een naar rechts
						uint8_t j=i;
						while((getlin(i,pixels)==1)&&i<12*13)i++;//zoek de 0
						if(i<12*13) setlin(i,1,pixels);
						setlin(j,0,pixels);
					}
				}
			}
		}
	}
}
	
void displayNumber(uint8_t n){
	//display a number (0..254) on the wordclock
	//this is intended to be used to display the ip-address of the clock
	for(uint8_t i=0; i<NUM_COLUMNS; i++) mypixels[i]=0;
	if(n>0 && n<=14) setwoord(n, mypixels);
	else if(n>=210 && n<=212) setwoord(n-200,mypixels);
	else setwoord(UREN_OFFSET+(n%10), mypixels);
	if(n>=15 && n<=149) setwoord(n/10, mypixels);
	if(n>=150 && n<=199) {
		setwoord(1, mypixels);
		setwoord((n-100)/10, mypixels);
	}
	if(n>=200){
		setwoord(2, mypixels);
		if(n<=209)setwoord(NUL_EXTRA,mypixels);
		if(n>=213 && n<=219)setwoord(1+UREN_OFFSET, mypixels);
		if(n>=220 && n<=222){
			setwoord(T_FOR_TWEE,mypixels);
			setwoord(T_FOR_TWEE+1,mypixels);
			setwoord(T_FOR_TWEE+2,mypixels);
		}
		if(n>=223 && n<=229)setwoord(2+UREN_OFFSET,mypixels);
		if(n>=230)setwoord((n-200)/10, mypixels);
	}	
	for(uint8_t i=0; i<NUM_COLUMNS; i++) pixels[i]=mypixels[i];
}

//this function displays the text "adres ip" on the wordklok
void display_text_ipadress(){
	for(uint8_t i=0; i<NUM_COLUMNS; i++) mypixels[i]=0;
	setwoord(I_FOR_IP,mypixels);
	setwoord(I_FOR_IP+1,mypixels);
	setwoord(A_FOR_ADRES,mypixels);
	setwoord(A_FOR_ADRES+1,mypixels);
	setwoord(A_FOR_ADRES+2,mypixels);
	for(uint8_t i=0; i<NUM_COLUMNS; i++) pixels[i]=mypixels[i];
}

void printchar(char c){
	while ( !( UCSR0A & (1<<UDRE0)) ){};//wait before transmit
	UDR0=c;	
}

void print(char*s){
	while (*s)printchar(*(s++));
}

void printint2(uint8_t n){
	//print  a digit 00..99 in two decimal characters
	//digits between 100 and 255 are printed with the first digit :;<=>?@ABC... e.g. 200 is D0
	printchar('0'+n/10);
	printchar('0'+n%10);
}

void printuint(uint8_t n){
	//print  a digit 0..255 in decimal characters
	if (n>=100) printchar('0'+n/100);
	if (n>=10)	printchar('0'+(n%100)/10);
	printchar('0'+n%10);
}

uint8_t calcDST(){
	//dagLIGHT SAVING TIME (DST)
	//calculates lastSundayInOctober (end of DST) , lastSundayInMarch (start of DST) and dst (now DST?)
	uint8_t zomertijd;
	
	if(!dst_enabled)return 0;

	//first calculate on which dag 31 Oct of this jaar falls (0=Sunday)
	uint8_t oct31dag=(jaar/4 + jaar + 2)%7;
	//now calculate on which dag 31 march falls
	uint8_t mar31dag=(oct31dag+3)%7;
	lastSundayInOctober=31-oct31dag;//end of DST
	lastSundayInMarch=31-mar31dag;//start of DST
	switch(maand){
		case 1:
		case 2:
		case 11:
		case 12:{
			zomertijd=0;
			break;
		}
		case 3:{
			if(dag<lastSundayInMarch){
				zomertijd=0;
				break;
			}
			if(dag>lastSundayInMarch){
				zomertijd=1;
				break;
			}
			//now we're on the last Sunday of March on which DST starts at 02:00 local wintertime 
			zomertijd= uur+timezone>=2; //hmm maybe this doesn't always work for negative timezones...
			break;
		}
		case 10:{
			if(dag<lastSundayInOctober){
				zomertijd=1;
				break;
			}
			if(dag>lastSundayInOctober){
				zomertijd=0;
				break;
			}
			//now we're on the last Sunday of October on which DST ends at 02:00 local wintertime
			zomertijd= uur+timezone<2;
			break;
		}
		default:{
			//the months April...September, all summertime
			zomertijd=1;
		}
	}
	return zomertijd;
}

//beschouw display als lineaire lijst van 0..155. (re)set led in die lijst
//het 12x13 display wordt als het ware uitgevouwen tot een 0..155 lineaire lijst
//value=0 (led uit) of 1 (led aan)
void setlin(uint8_t index,uint8_t value, pixel16_t*matrix){
	uint8_t rij; //rij 0 bevat de secondeleds, rij 1-12 bevatten letters, start bovenaan
	uint8_t kolom;//kolom 0..12, start links
	rij=index/13;
	kolom=index%13;
	if((rij&1))kolom=13-1-kolom;
	rij++;
	uint16_t pattern= 1<<(rij+3);
	if(value) matrix[kolom]|=pattern;
	else	matrix[kolom]&= ~pattern;
}

uint8_t getlin(uint8_t index, pixel16_t*matrix){
	uint8_t rij; //rij 0 bevat de secondeleds, rij 1-12 bevatten letters, start bovenaan
	uint8_t kolom;//kolom 0..12, start links
	rij=index/13;
	kolom=index%13;
	if((rij&1))kolom=13-1-kolom;
	rij++;
	uint16_t pattern= 1<<(rij+3);
	if(matrix[kolom] & pattern) return 1;
	else return 0;
}

//call this once per second to do the real time clock housekeeping
void tick1s(){
	tic++;//this is a second counter that just loops around 255 and is never set
	sec++;
	if(sec==60){
		min++;
		sec=0;
		if(min==60){
			uur=(uur+1)%24;
			min=0;
			if(uur==0){
				dag++;
				//berekening maand, jaar nog te doen
				//maar hoeft eigenlijk niet want is alleen van belang voor de zomertijdberekening
				//en ik ga ervan uit dat er een maand wel een keer een datum van NTP binnenkomt 
				//en de zomertijdwisselingen zijn altijd in het eind van de maand
			}
		}
	}
}

//interrupt routine for serial input
ISR(USART_RX_vect){
	static uint8_t inside_brackets=0;
	char k=UDR0;
	if(k=='<')inside_brackets=1;
	if(inside_brackets && buffyIndex<BUFFYSIZE){
		buffy[buffyIndex]=k;
		buffyIndex++;
	}
	if(k=='>')inside_brackets=0;	
}

//interrupt routine for timer 1: every ms draw one column
//As there are 13 columns, a complete screen takes 13ms
//makes a screen rate of 77 Hz, which is fine.
ISR(TIMER1_OVF_vect){
	//PIND|=(1<<2); //toggle PD2, just for debugging
	static 	uint8_t column=0;

	SPDR=pixels[column]&0xff;//send LSB
	while(!(SPSR & (1<<SPIF))) {}//wait for transfer to complete
	SPDR=pixels[column]>>8;//send MSB
	while(!(SPSR & (1<<SPIF))) {}//wait for transfer to complete
	//PORTB|= (1<<1);//output disable on the MBI5026 on PB1; this is now done in hardware (PWM output OC1A)
	delay_ms(1);
	PORTD|= (1<<6); //clock for columns
	PORTB|= (1<<2);//latch for rows
	PORTD|= (1<<7); //latch for columns
	PORTB&=~(1<<2);
	PORTD&=~(1<<6);
	PORTD&=~(1<<7);
	//PORTB&=~(1<<1);//output enable on the MBI5026 on PB1; this is now done in hardware (PWM output OC1A)
	
	column=(column+1)%NUM_COLUMNS;
	if(column==0)PORTD|=(1<<5); //PD5 on
	else PORTD&=~(1<<5);
}

//interrupt routine for timer 2; called 125 times per second (rtc timer housekeeping)
ISR(TIMER2_COMPA_vect){
	static uint8_t cntr125=125;
	cntr125--;
	if(cntr125==0){
		tick1s();
		cntr125=125;
	}
}

