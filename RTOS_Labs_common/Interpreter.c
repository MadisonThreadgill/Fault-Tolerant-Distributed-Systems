// *************Interpreter.c**************
// Part of Lab 1, will add onto this as necessary for other labs
// Madison Threadgill, Burak Biyikli
// 1/20/23
// Lab 1 main program that will call interpreter.c
// Lab 1
// Noah
// 1/24/23
// Students implement this as part of EE445M/EE380L.12 Lab 1,2,3,4 
// High-level OS user interface
// 
// Runs on LM4F120/TM4C123
// Jonathan W. Valvano 1/18/20, valvano@mail.utexas.edu
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../RTOS_Labs_common/OS.h"
#include "../RTOS_Labs_common/ST7735.h"
#include "../inc/ADCT0ATrigger.h"
#include "../inc/ADCSWTrigger.h"
#include "../RTOS_Labs_common/UART0int.h"
#include "../RTOS_Labs_common/eDisk.h"
#include "../RTOS_Labs_common/eFile.h" 
#include "../RTOS_Labs_common/ADC.h"
#include "../inc/LED.h"
#include "../RTOS_Labs_common/esp8266.h"

uint32_t NumCreated;           // number of foreground threads created
uint32_t NumSamples;   				 // incremented every ADC sample, in Producer
uint32_t PIDWork;              // current number of PID calculations finished
uint32_t FilterWork;           // number of digital filter calculations finished
int32_t x[64],y[64];           // input and output arrays for FFT
uint32_t DataLost;             // data sent by Producer, but not received by Consumer 
uint32_t CPUUtil;
//extern Sema4Type WebServerSema;
//uint32_t JitterHistogram[];
//nt32_t const JitterSize;
int32_t MaxJitter;             // largest time jitter between interrupts in usec

#define NUM_COMMANDS 10
#define I_TASK_COUNT 8
#define I_TASK_NAME_LEN 7

void LcdHelpScreen(void);
void AdcHelpScreen(void);
void UartHelpScreen(void);
void LedHelpScreen(void);
void MetricHelpScreen(void);

void I_ADC( char *arg1, char *arg2, char* arg3, char* arg4);
void I_LCD( char *arg1, char *arg2, char* arg3, char* arg4);
void I_LED( char *arg1, char *arg2, char* arg3, char* arg4);
void I_LS( char *arg1, char *arg2, char* arg3, char* arg4);
void I_HELP( char *arg1, char *arg2, char* arg3, char* arg4);
void I_TIME( char *arg1, char *arg2, char* arg3, char* arg4);
void I_UART( char *arg1, char *arg2, char* arg3, char* arg4);
void I_METRIC(char *arg1, char *arg2, char* arg3, char* arg4);

struct Interpreter_Task{
    char name[I_TASK_NAME_LEN+1];
    void (*fpt)(char*, char*, char*, char*);
    void (*hpt)(void);
};

struct Interpreter_Task I_Task_List[I_TASK_COUNT] = {
    {"adc",    I_ADC,    AdcHelpScreen },
    {"lcd",    I_LCD,    LcdHelpScreen },
    {"led",    I_LED,    LedHelpScreen },
    {"ls",     I_LS,     NULL},
    {"help",   I_HELP,   NULL},
    {"time",   I_TIME,   NULL},
    {"uart",   I_UART,   UartHelpScreen},
    {"metric", I_METRIC, MetricHelpScreen}
};

void MetricHelpScreen(void){
    UART_OutString("\n\r");
	UART_OutString("NAME\n\r");
	UART_OutString("\tmetric - display a performance metric\n\r");
	UART_OutString("\n\r");

	UART_OutString("DESCRIPTION\n\r");
	UART_OutString("\tCan display a particular performance metric\n\r");
	UART_OutString("\n\r");
	
	UART_OutString("USAGE\n\r");
	UART_OutString("\tmetric [selected metric]\n\r");
	UART_OutString("\n\r");
	UART_OutString("\tselected metric: NumSamples, NumCreated, MaxJitter, DataLost, FilterWork, PIDwork, FFT, JitterHist,critical\n\r");
	UART_OutString("\tNumSamples    : Number of samples \n\r");
    UART_OutString("\tNumCreated    : Number of threads created \n\r");
    UART_OutString("\tMaxJitter     : Maximum time jitter \n\r");
    UART_OutString("\tJitterHist    : Jitter Histogram\n\r");
	UART_OutString("\tDataLost      : Amount of data lost\n\r");
	UART_OutString("\tFilterWork    : Work done by filter \n\r");
	UART_OutString("\tPIDwork		: Work done by PID\n\r");
	UART_OutString("\tFFT   		: Data in FFT arrays\n\r");
    UART_OutString("\tcritical  	: Number of cycles in/outCritical Sections\n\r");
	UART_OutString("\n\r");
}

// displays help screen over UART for lcd screen command
// no inputs or outputs
void LcdHelpScreen(void){
	UART_OutString("\n\r");
	UART_OutString("NAME\n\r");
	UART_OutString("\tlcd - write to the lcd screen\n\r");
	UART_OutString("\n\r");

	UART_OutString("DESCRIPTION\n\r");
	UART_OutString("\tcan write a string and a number to a specified row and screen on the lcd display\n\r");
	UART_OutString("\n\r");
	
	UART_OutString("USAGE\n\r");
	UART_OutString("\tlcd [screen options] [row number] [\"string\"] [number]\n\r");
	UART_OutString("\n\r");
	UART_OutString("\tscreen options: top, bottom \n\r");
	UART_OutString("\trow number    : 0 through 7 \n\r");
	UART_OutString("\tstring        : string to be printed (surrounded by \"\")\n\r");
	UART_OutString("\tnumber        : integer number to be printed \n\r");
	UART_OutString("\n\r");
}

// displays help screen over UART for adc command
// no inputs or outputs
void AdcHelpScreen(void){
	UART_OutString("\n\r");
	UART_OutString("NAME\n\r");
	UART_OutString("\tadc - read analog input from specified pin\n\r");
	UART_OutString("\n\r");

	UART_OutString("DESCRIPTION\n\r");
	UART_OutString("\tchannel number (0 to 11) specifies which pin is sampled with sequencer 3\n\r");
	UART_OutString("\tif channel number is greater than 11, adc will return an error\n\r");
	UART_OutString("\tThe value of the ADC can also be printed to the screen\n\r");
	UART_OutString("\n\r");
	
	UART_OutString("USAGE\n\r");
	UART_OutString("\tadc [channel number] | [read]\n\r");
	UART_OutString("\n\r");
	UART_OutString("\tchannel number: 0 through 11 \n\r");
	UART_OutString("\t			read: \"read\" will print ADC value to the screen \n\r");
	UART_OutString("\n\r");
}

// displays help screen over UART for uart command
// no inputs or outputs
void UartHelpScreen(void){
UART_OutString("\n\r");
  UART_OutString("NAME\n\r");
  UART_OutString("\tuart - print message to screen via uart\n\r");
  UART_OutString("\n\r");

  UART_OutString("DESCRIPTION\n\r");
  UART_OutString("\tstring printed to the terminal are any characters after \"uart\"\n\r");
  UART_OutString("\n\r");
	
  UART_OutString("USAGE\n\r");
  UART_OutString("\tuart [string]\n\r");
  UART_OutString("\n\r");
  UART_OutString("\tstring: string to be printed\n\r");
  UART_OutString("\n\r");
}

// displays help screen over UART for led command
// no inputs or outputs
void LedHelpScreen(void){
    
  UART_OutString("NAME\n\r");
  UART_OutString("\tled - turn on or off a specific led\n\r");
  UART_OutString("\n\r");

  UART_OutString("DESCRIPTION\n\r");
  UART_OutString("\tcan turn on or off a red, blue, or green led\n\r");
  UART_OutString("\n\r");
	
  UART_OutString("USAGE\n\r");
  UART_OutString("\tled [color] [option]\n\r");
  UART_OutString("\n\r");
  UART_OutString("\tcolor : can choose \"red\", \"green\", \"blue\", or \"all\" \n\r");
  UART_OutString("\toption: can turn led \"on\" or \"off\"\n\r");
  UART_OutString("\n\r");
}

uint32_t parseDec( char *arg){
    uint32_t number=0;
    char sign = 0;
    if( arg == NULL) 
        return 0;
    if( *arg == '-'){
        sign = 1;
        arg = arg+1;
    }
    while( '0' <= *arg && *arg <= '9' ){ 
        number = 10*number+(*arg-'0');   // this line overflows if above 4294967295
        arg = arg+1;
    }
    return sign == 1 ? -number : number;
}

void I_ADC( char *arg1, char *arg2, char* arg3, char* arg4){
    if(arg1 == NULL){
        UART_OutString("\r\n Invalid ADC command, try --help");
    } else if( strcmp(arg1, "read") == 0){		
        UART_OutString("\r\nADC value: ");
        UART_OutUDec(ADC_In()*4/5);
        UART_OutString(" mv");
    } else {
        int ch = parseDec(arg1);
        ADC_Init(ch);
        UART_OutString("\n\rADC set to ch ");
        UART_OutUDec(ch);
        UART_OutString("\n\rReading: ");
        UART_OutUDec(ADC_In()*4/5);
        UART_OutString(" mv");
    }
}
void I_LCD( char *arg1, char *arg2, char* arg3, char* arg4){
    if( arg1 == NULL || arg2 == NULL || arg3 == NULL){
        UART_OutString("\n\rFailed!");
        return;
    }
    int d = parseDec(arg1);
    int l = parseDec(arg2);
    int v = arg4==NULL ? 0x8000 : parseDec(arg4);
    ST7735_Message(d,l,arg3,v);
    UART_OutString("\n\rDone!");
}

void I_LED( char *arg1, char *arg2, char* arg3, char* arg4 ){
    LED_Init();
    if(strcmp(arg1, "red") == 0){   // commands for red led
		if(strcmp(arg2, "off") == 0){      // turn red led off
			LED_RedOff();
		} else if(strcmp(arg2, "on") == 0){  // turn red led on
			LED_RedOn();
		}
	} else if(strcmp(arg1, "green") == 0){ // commands for red led
		if(strcmp(arg2, "off") == 0){      // turn green led off
			LED_GreenOff();
		} else if(strcmp(arg2, "on") == 0){  // turn green led on
			LED_GreenOn();
		}
	} else if(strcmp(arg1, "blue") == 0){  // commands for blue led
		if(strcmp(arg2, "off") == 0){      // turn blue led off
			LED_BlueOff();
		} else if(strcmp(arg2, "on") == 0){  // turn blue led on
			LED_BlueOn();
		}
	} else if(strcmp(arg1, "all") == 0){   // commands for all leds 
		if(strcmp(arg2, "off") == 0){      // turn all leds off
			LED_BlueOff();
			LED_GreenOff();
			LED_RedOff();
		} else if(strcmp(arg2, "on") == 0){  // turn all leds on
			LED_BlueOn();
			LED_GreenOn();
			LED_RedOn();
        }
    }
}

void I_LS( char *arg1, char *arg2, char* arg3, char* arg4){
    UART_OutString("\n\rNo FS Loaded!");
}

void I_HELP( char *arg1, char *arg2, char* arg3, char* arg4){
    UART_OutString("\n\r You can use the following commands:");
    for( int i=0; i<I_TASK_COUNT; i++){
        UART_OutString("\n\r -> ");
        UART_OutString( I_Task_List[i].name );   
    }
    UART_OutString("\n\rType [command] --help for usage");
}

void I_TIME( char *arg1, char *arg2, char* arg3, char* arg4){
    if( arg1 == NULL) {
        UART_OutString( "\n\rCurrent Time: ");
        UART_OutUDec( OS_MsTime() );
    } else {
        OS_ClearMsTime();
        UART_OutString( "\n\rCleared time!");
    }
}

void I_UART( char *arg1, char *arg2, char* arg3, char* arg4){
  UART_OutString("\n\r");
  UART_OutString(arg1);			
}

void I_METRIC(char *arg1, char *arg2, char* arg3, char* arg4){
    if(strcmp(arg1, "NumCreated") == 0){   // command for number of threads
        UART_OutString("\n\rNumber of threads created: ");
        UART_OutUDec(NumCreated);
    } else if(strcmp(arg1, "MaxJitter") == 0){  // command for max jitter
        int ch = parseDec(arg2) == 0 ? 0 : 1;
        uint32_t MaxJitter;
        OS_Report_Jitter(ch, &MaxJitter);
        UART_OutString("\n\rMaximum time jitter (us): ");
				MaxJitter = MaxJitter / 80;
        UART_OutUDec(MaxJitter);
    } else if(strcmp(arg1, "NumSamples") == 0){  // command for number of samples
        UART_OutString("\n\rNumber of Samples: ");
        UART_OutUDec(NumSamples);
    } else if(strcmp(arg1, "DataLost") == 0){  // command for data lost
        UART_OutString("\n\rAmount of Data Lost: ");
        UART_OutUDec(DataLost);
    } else if(strcmp(arg1, "PIDwork") == 0){  // command for PID work
        UART_OutString("\n\rAmount of PIDwork: ");
        UART_OutUDec(PIDWork);
    } else if(strcmp(arg1, "FilterWork") == 0){  // command for filter work
        UART_OutString("\n\rAmount of Filter Work: ");
        UART_OutUDec(FilterWork);
    } else if(strcmp(arg1, "FFT") == 0){ // command for FFT array
			UART_OutString("\n\rData in FFT Arrays: ");
			UART_OutString("\n\rx  y");
			UART_OutString("\n\r");
			for(int i = 0; i < 64; i++){
				UART_OutUDec(x[i]);
				UART_OutString("  ");
				UART_OutUDec(y[i]);
				UART_OutString("\n\r");
			}
    } else if(strcmp(arg1, "JitterHist") == 0){ // command for Jitter Histogram
            int ch = parseDec(arg2) == 0 ? 0 : 1;
            uint32_t MaxJitter,*JitterData;
			UART_OutString("\n\rJitter Histogram ");
            UART_OutUDec(ch);
            UART_OutString(" :");
            UART_OutString("\n\rCycles : Count");
			JitterData = OS_Report_Jitter(ch, &MaxJitter);
            for(uint32_t i=0; i<256; i++){
                UART_OutString("\n\r");
                UART_OutUDec(i);
                UART_OutString(" : ");
                UART_OutUDec(JitterData[i]);
            }
            UART_OutString("\n\r Max Jitter (cycles): ");
            UART_OutUDec(MaxJitter);
    } else if(strcmp(arg1, "CPUUtil") == 0){
			UART_OutString("\n\r CPU Utlization: ");
      UART_OutUDec(CPUUtil);
		} else if ( strcmp(arg1, "critical") == 0 ){
        #ifdef EN_DEBUG_PROFILING
            uint32_t inC, inCMax, outC;
            OS_Report_Critical( &inC, &outC, &inCMax);
            UART_OutString("\n\r Critical Stats : ");
            UART_OutString("\n\r In  :");
            UART_OutUDec(inC);
            UART_OutString("\n\r Out :");
            UART_OutUDec(outC);
            UART_OutString("\n\r InMax :");
            UART_OutUDec(inCMax);
						UART_OutString("\n\r Percentage of Time with Interrupts Disabled : ");
            UART_OutUDec((inC/outC)*100);
        #else
            UART_OutString("\n\r Recompile with EN_DEBUG_PROFILING to collect this data");
        #endif 
    
    }
		else if( strcmp(arg1, "clear") == 0){
			OS_ResetMetrics();
			UART_OutString("\n\r Metrics Cleared");
		}
		else {
        UART_OutString("\n\rCommand Not recognized printing help: ");
        MetricHelpScreen();
    }
}




// *********** Command line interpreter (shell) ************

char bufPt[40];
char bufPtCP[40];
void Interpreter(void){ 
  // Get string from command line until user presses enter
  
  int i,j;
  char *segments[4];
  char segment_index;
  void (*nfpt)(char*, char*, char*, char*);
  void (*nhpt)(void);
  char qoute;
	

  while(1) {
		
    UART_OutString("\n\r>");
		UART_InString(bufPt, 40);

    nfpt = NULL;
    nhpt = NULL;
            
    //Function Lookup, Could do Binary search, but is not currently worth it
    for( i=0; i<I_TASK_COUNT; i++){
        for( j=0; j<=I_TASK_NAME_LEN; j++){
            if( I_Task_List[i].name[j] == 0 && (bufPt[j] == ' ' || bufPt[j] == 0 || bufPt[j] == CR) ){
                nfpt = I_Task_List[i].fpt;
                nhpt = I_Task_List[i].hpt;
                break;
            } else if( I_Task_List[i].name[j] != bufPt[j] ) {
                break;
            }
        }  
    }
    
    //Segmentation, find location of arguments
    segment_index =0;
    qoute = 0;
    for( i=0; i<39; i++){
        bufPtCP[i] = bufPt[i];
        if ( bufPt[i] == 0 ) {
            for( j=segment_index; j<4; j++){
                segments[j] = NULL;
            }
        } else if ( qoute == 1 ){
            qoute         = bufPt[i] == '"' ? 0 : 1;
            bufPt[i] = bufPt[i] == '"' ? 0 : bufPt[i];
        } else if ( qoute==0 && bufPt[i] == '"') {
            segments[segment_index] = bufPt+i+1;
            segment_index = segment_index+1;
            qoute = 1;
        }else if ( bufPt[i] == ' '){
            bufPt[i] = 0;
            if (bufPt[i+1] != ' ' && bufPt[i+1] != 0 && bufPt[i+1] != '"' ){
                segments[segment_index] = bufPt+i+1;
                segment_index = segment_index+1;
            }   
        }
    }
    
    if(segments[0] != NULL && strcmp(segments[0], "--help") == 0 && nhpt != NULL){
        nhpt();
    } else if ( nfpt == NULL){
        I_HELP(segments[0], segments[1], segments[2], segments[3]);
    } else if ( nfpt == I_UART ) {
        nfpt(bufPtCP, segments[0], segments[1], segments[2]);
    } else {
        nfpt(segments[0], segments[1], segments[2], segments[3]);
    }
		
  }
}

// *********** Remote Command line interpreter (Telnet) ************

char* my_strtok(char* str, const char* delimiters) {
    static char* token = NULL;
    if (str) {
        token = str;
    } else if (!token) {
        return NULL;
    }

    char* result = token;
    while (*token) {
        const char* d = delimiters;
        while (*d) {
            if (*token == *d) {
                *token++ = '\0';
                return result;
            }
            d++;
        }
        token++;
    }
    token = NULL;
    return result;
}

void PROCESS_CMD_REMOTE(char *in)	{
	
	char* cmd = my_strtok(in, " ");
	char *arg = my_strtok(NULL, " ");
	
	if(strcmp(in, "led_red_on") == 0)	{
		LED_RedOn();
	} else if (strcmp(in, "led_red_off") == 0)	{
		LED_RedOff();
	} else if (strcmp(in, "led_blue_on") == 0)	{
		LED_BlueOn();
	} else if (strcmp(in, "led_blue_off") == 0)	{
		LED_BlueOff();
	} else if (strcmp(in, "clear") == 0)	{
		OS_ResetMetrics();
		ESP8266_Send("\n\r Metrics Cleared");
	} else if (strcmp(in, "exit") == 0)	{
		ESP8266_Send("\n\r Closing Remote Interpreter\n\r");
		ESP8266_CloseTCPConnection();
		//OS_Signal(&WebServerSema);
		OS_Kill();
	}	else	{
		ESP8266_Send("Command not found\n\r");
	}
	
}

void Interpreter_Remote(void)	{
	
	while(1)	{
		
		char cmd[100];
    ESP8266_Send("\n\rCMD> ");
    ESP8266_Receive(cmd,100);
    ESP8266_Send("\n\r");
    PROCESS_CMD_REMOTE(cmd);
		
	}
	
}


