/*
 * WoordKlokCharles.c
 *
 * Created: 6-4-2018 14:10:04
 *  Author: Charles
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

void setpixels();
void printchar(char c);
void print(char*s);
uint8_t calcDST();
void checkSerialinput();
void checkLdr();
void setlin(uint8_t index,uint8_t value, uint16_t*matrix);
uint8_t getlin(uint8_t index, uint16_t*matrix);


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
	8,10,3,//EEN uren 16
	9,0,4, //TWEE 17
	9,5,4, //DRIE 18
	9,9,4, //VIER 19
	10,0,4,//VIJF 20
	10,4,3,//ZES 21
	10,8,5,//ZEVEN 22
	11,0,4,//ACHT 23
	11,4,5,//NEGEN 24
	11,9,4,//TIEN 35
	12,0,3,//ELF 26
	12,3,6,//TWAALF 27
	1,4,2, //IS 28
	7,9,4, //VOOR 29
	8,0,4, //OVER 30
	8,5,4, //HALF 31
	12,10,3,//UUR 32
	0,12,1,//SEC00_14 de vier hoekleds zitten op rij 0
	0,11,1,//SEC15_29 34
	0,1,1, //SEC30_44 35
	0,0,1  //SEC45_59 36
	
};

#define UREN_OFFSET 15
#define HET 0
#define IS 28
#define VOOR 29
#define OVER 30
#define HALF 31
#define UUR 32
#define SEC00_14 33
#define SEC15_29 34
#define SEC30_44 35
#define SEC45_59 36

#define NUM_COLUMNS 13
#define KLKDBG 1

volatile uint8_t jaar=0;//alleen de laatste twee cijfers van het jaar.
volatile uint8_t maand=0;
volatile uint8_t dag=0;
volatile uint8_t uur=0;
volatile uint8_t min=0;
volatile uint8_t sec=0;
static uint8_t timezone=1; //lokale tijdzone: 1 uur verschil met UTC
volatile uint8_t dst=0; //daglight saving time (zomertijd)
uint8_t lastSundayInOctober;//end of DST
uint8_t lastSundayInMarch;//start of DST

//variables for the LDR
uint16_t sumLdrSamples=0;
uint8_t numLdrSamples=0;

uint8_t bufindex=0;
char buf[13];

static uint16_t pixels[NUM_COLUMNS];//each uint16_t stores 1 column of pixels
static uint16_t mypixels[NUM_COLUMNS];//prepare the pixels in this array first then later copy everything to pixels[]

void delay_ms(uint16_t x)
{
	uint8_t y, z;
	for ( ; x > 0 ; x--){
		for ( y = 0 ; y < 90 ; y++){
			for ( z = 0 ; z < 6 ; z++){
				asm volatile ("nop");
			}
		}
	}
}

int main(void)
{
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
	OCR1A=224;//at 10% brightness
	OCR1A=124;//at 50% brightness
	OCR1A=24; //at 90% brightness, this is about maximum otherwise ghosting will appear
	TIMSK1=(1<<TOIE1);

	//setup 8 bit timer/counter 2 in CTC mode (mode 2), F=fcpu/1024, for the real time clock, generates interrupt every 8ms
	TCCR2A=(1<<WGM21);
	TCCR2B=(1<<CS20)|(1<<CS21)|(1<<CS22);
	OCR2A=124;
	TIMSK2=(1<<OCIE2A);

	sei();//enable interrupts

	//setup UART AT 115k2b/s for communication with the ESP01 (which communicates with wifi, internet and ntp time server)
	UCSR0A=(1<<U2X0); //double speed
	UCSR0B=(1<<RXEN0)|(1<<TXEN0);//enable tx & rx
	UCSR0C=(1<<UCSZ00)|(1<<UCSZ01)|(1<<USBS0);//8 bit, 2 stop bits
	UBRR0=16;//set speed at 117k6 b/s, which is 2% high of 115k2
	print("Klok");
	
	
	//setup the ADC at 140kHz (16MHz/128), input is ADC0=PC0, reference is 1.1V
	//it is used fot the Light Dependent Resistor (LDR) to determine ambient light level
	//when there is bright light, the LDR will have a low value and the voltage on ADC0 will be low (say, 0.4V)
	//when there is darkness, the LDR will have a high value and the voltage on ADC0 will be high (1.1V max)
	ADCSRA=(1<<ADEN)|(1<<ADPS0)|(1<<ADPS1)|(1<<ADPS2);
	ADMUX=(1<<REFS0)|(1<<REFS1);
	ADCSRA|=(1<<ADSC);//start conversion
	
	for(uint8_t k=0; k<NUM_COLUMNS; k++) pixels[k]=0;

    while(1){
		setpixels();
		checkSerialinput();
		checkLdr();
	}
}

void checkSerialinput(){
	//check serial port input. We expect "T22:23:24> or "D2017-12-31>"
	if(UCSR0A&(1<<RXC0)){
		//there is serial data received
		char k=UDR0;
		if(bufindex==0){//wait for a T (time) or D (date)
			if((k=='T') || (k=='D')){
				buf[0]=k;
				bufindex=1;
				printchar('<');
			}
		}else{
			if(k=='>'){
				buf[bufindex++]=k;
				buf[bufindex]=0;
				//now do something with the string received!
				//echo it
				print(buf);
				if(buf[0]=='T'){
					//do syntax check: should be like T22:23:24Z
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
							
						dst=calcDST();//calculate zomertijd

						cli();//temporily disable interrupts
						//set the time
						uur=(10*(buf[1]-'0') + (buf[2]-'0') + timezone + dst - 1)%12 + 1; //waarden: 1..12
						min=10*(buf[4]-'0')+buf[5]-'0';
						sec=10*(buf[7]-'0')+buf[8]-'0';
						sei();
					}
				}else{
					//do syntax check: should be like D2017-02-31Z
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
				}
				bufindex=0;
			}else{
				if(bufindex<11){
					buf[bufindex++]=k;
				}else{
					//error: ">" not received in time!
					bufindex=0;
					//print(buf);
					//printchar('E');
				}
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
			uint16_t ldr=sumLdrSamples/30;
			if(ldr<141)ldr=141;
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

void setwoord(uint8_t woord, uint16_t*matrix){
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

/*
void setpixels_old(){
	pixels[0]=(1<<(5+sec%10))-1;
	pixels[1]=(1<<(5+sec/10))-1;
	pixels[2]=0;
	pixels[3]=(1<<(5+min%10))-1;
	pixels[4]=(1<<(5+min/10))-1;
	pixels[5]=0;
	pixels[6]=(1<<(5+uur%10))-1;
	pixels[7]=(1<<(5+uur/10))-1;
	pixels[8]=0;
	pixels[9]=0;
	pixels[10]=0;
	pixels[11]=0;
	pixels[12]=0;
}
*/

void setpixels(){
	static uint8_t prevsec=0;
	uint8_t newsec=sec;
	if(newsec!=prevsec){
		prevsec=newsec;
		for (uint8_t i=0; i<NUM_COLUMNS; i++) mypixels[i]=0;
		
		/*
		//TOON DE SECONDES OP HET SCHERM		
		mypixels[10]=(1<<(4+sec%10))-1;
		mypixels[9]=(1<<(4+sec/10))-1;
		*/

		setwoord(HET, mypixels);
		setwoord(IS, mypixels);
	
		if(KLKDBG){
			printchar(10);
			printchar(13);
			printchar('2');
			printchar('0');
			printchar('0'+jaar/10);
			printchar('0'+jaar%10);
			printchar('-');
			printchar('0'+maand/10);
			printchar('0'+maand%10);
			printchar('-');
			printchar('0'+dag/10);
			printchar('0'+dag%10);
			printchar('T');
			printchar('0'+uur/10);
			printchar('0'+uur%10);
			printchar(':');
			printchar('0'+min/10);
			printchar('0'+min%10);
			printchar(':');
			printchar('0'+sec/10);
			printchar('0'+sec%10);
			printchar('+');
			printchar('0');
			printchar('0'+timezone+dst);
			printchar('0');
			printchar('0');
			printchar(' ');
			print("HET IS ");
		}
		if(min==0){
			setwoord(UUR, mypixels);
			if(KLKDBG)print("UUR ");
		}
		else{
			uint8_t minutenweergave=min%30;
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
				if((min<=15)||((min>30)&&(min<45))){
					setwoord(OVER, mypixels);
					if(KLKDBG)print("OVER ");
				}else{
					setwoord(VOOR, mypixels);
					if(KLKDBG)print("VOOR ");
				}
			}
			if((min>15)&&(min<45)){
				setwoord(HALF, mypixels);
				if(KLKDBG)print("HALF ");
			}
		}
		uint8_t urenweergave=uur;
		if(min>15)urenweergave++;
		urenweergave=(urenweergave+11)%12+1;
		setwoord(UREN_OFFSET+urenweergave, mypixels);
		if(KLKDBG){
			printchar('0'+(urenweergave/10));
			printchar('0'+(urenweergave%10));
		}
		
		//De vier 15seconden-LED's, zet direct in pixels, niet eerst in mypixels
		pixels[0]&=~(1<<3);
		pixels[1]&=~(1<<3);
		pixels[11]&=~(1<<3);
		pixels[12]&=~(1<<3);
		setwoord((sec/15)+SEC00_14, pixels);
		if(KLKDBG){
			printchar('s');
			printchar('0'+(sec/15));
		}
		setlin(0,getlin(0,mypixels),pixels);
		setlin(1,getlin(1,mypixels),pixels);
		setlin(11,getlin(11,mypixels),pixels);
		setlin(12,getlin(12,mypixels),pixels);
		//for (uint8_t i=0; i<NUM_COLUMNS; i++) pixels[i]=mypixels[i];
	}
	
	//todo
	static uint16_t slow=0;
	slow++;
	slow&=0x1fff;
	if(slow==0){
		//vergelijk mypixels (de gewenste weergave) met pixels (de huidige weergave). Doe dit pixel voor pixel. Vind je een verschil, verschuif dan een beetje zodat
		//pixels iets beter gaat lijken op mypixels
		//zoek het eerste verschil
		uint8_t i;
		for(i=0;i<12*13; i++){
			if (getlin(i,pixels)!=getlin(i,mypixels)) break;
		}
		if(i<12*13){//zijn er uberhaupt verschillen?
			if(getlin(i,mypixels)){//schuif een 1 naar links
				while((getlin(i,pixels)==0)&&i<12*13)i++;//zoek de 1
				if(i<12*13) setlin(i,0,pixels);
				setlin(i-1,1,pixels);
			}else{//schuif een een naar rechts
				uint8_t j=i;
				while((getlin(i,pixels)==1)&&i<12*13)i++;//zoek de 0
				if(i<12*13) setlin(i,1,pixels);
				setlin(j,0,pixels);
			}
		}
	}
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

//call this once per second to do the real time clock housekeeping
void tick1s(){
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
				//en ik ga ervan uit dat er op een dag wel een keer een datum van NTP binnenkomt...
			}
		}
	}
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

void printchar(char c){
	while ( !( UCSR0A & (1<<UDRE0)) ){};//wait before transmit
	UDR0=c;	
}

void print(char*s){
	while (*s)printchar(*(s++));
}

uint8_t calcDST(){
	//dagLIGHT SAVING TIME (DST)
	//calculates lastSundayInOctober (end of DST) , lastSundayInMarch (start of DST) and dst (now DST?)
	uint8_t zomertijd;
	
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
			//now we're on the last Sunday of March on which DST starts at 02:00 standard time
			zomertijd= uur>=2;
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
			//now we're on the last Sunday of October on which DST ends at 02:00 standard time
			zomertijd= uur<2;
			break;
		}
		default:{
			//the maands April...September, all summertime
			zomertijd=1;
		}
	}
	return zomertijd;
}

//beschouw display als lineaire lijst van 0..155. (re)set led in die lijst
//van deze functies kunnen verschillende varianten gemaakt worden, afhankelijk van hoe het 12x13 display wordt opgerold tot 0..155 lineair
//value=0 (led uit) of 1 (led aan)
void setlin(uint8_t index,uint8_t value, uint16_t*matrix){
	uint8_t rij; //rij 0 bevat de secondeleds, rij 1-12 bevatten letters, start bovenaan
	uint8_t kolom;//kolom 0..12, start links
	rij=index/13;
	kolom=index%13;
	if(rij&1)kolom=13-1-kolom;
	rij++;
	uint16_t pattern= 1<<(rij+3);
	if(value) matrix[kolom]|=pattern;
	else	matrix[kolom]&= ~pattern;
}

uint8_t getlin(uint8_t index, uint16_t*matrix){
	uint8_t rij; //rij 0 bevat de secondeleds, rij 1-12 bevatten letters, start bovenaan
	uint8_t kolom;//kolom 0..12, start links
	rij=index/13;
	kolom=index%13;
	if(rij&1)kolom=13-1-kolom;
	rij++;
	uint16_t pattern= 1<<(rij+3);
	if(matrix[kolom] & pattern) return 1;
	else return 0;
}
