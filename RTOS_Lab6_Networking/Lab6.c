// Lab6.c
// Runs on LM4F120/TM4C123
// Real Time Operating System for Lab 6

// Jonathan W. Valvano 3/29/17, valvano@mail.utexas.edu
// Andreas Gerstlauer 3/1/16, gerstl@ece.utexas.edu
// EE445M/EE380L.6 
// You may use, edit, run or distribute this file 
// You are free to change the syntax/organization of this file

// LED outputs to logic analyzer for use by OS profile 
// PF1 is preemptive thread switch
// PF2 is first periodic background task (if any)
// PF3 is second periodic background task (if any)
// PC4 is PF4 button touch (SW1 task)

// Outputs for task profiling
// PD0 is idle task
// PD1 is button task

// Button inputs
// PF0 is SW2 task
// PF4 is SW1 button input

// Analog inputs
// PE3 Ain0 sampled at 2kHz, sequencer 3, by Interpreter, using software start

#include <stdint.h>
#include <stdbool.h> 
#include <stdio.h> 
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "../inc/tm4c123gh6pm.h"
#include "../inc/CortexM.h"
#include "../inc/LaunchPad.h"
#include "../inc/PLL.h"
#include "../inc/LPF.h"
#include "../RTOS_Labs_common/UART0int.h"
#include "../RTOS_Labs_common/ADC.h"
#include "../RTOS_Labs_common/OS.h"
#include "../RTOS_Labs_common/heap.h"
#include "../RTOS_Labs_common/Interpreter.h"
#include "../RTOS_Labs_common/ST7735.h"
#include "../RTOS_Labs_common/eDisk.h"
#include "../RTOS_Labs_common/can0.h"
#include "../RTOS_Labs_common/esp8266.h"
#include "../inc/Timer.h"
#include "../inc/ADCSWTrigger.h"
#include "../inc/IRDistance.h"


// CAN IDs are set dynamically at time of CAN0_Open
// Reverse on other microcontroller
#define RCV_ID 4
#define XMT_ID 2

//*********Prototype for PID in PID_stm32.s, STMicroelectronics
short PID_stm32(short Error, short *Coeff);
short IntTerm;     // accumulated error, RPM-sec
short PrevError;   // previous error, RPM

//uint32_t NumCreated;   // number of foreground threads created
uint32_t IdleCount;    // CPU idle counter
#define BUFFER_SIZE 1500
char buffer[BUFFER_SIZE];
#define MATRIX_SIZE 20
uint32_t matrix_1[MATRIX_SIZE][MATRIX_SIZE];
uint32_t matrix_2[MATRIX_SIZE][MATRIX_SIZE];
uint32_t product_matrix[MATRIX_SIZE][MATRIX_SIZE];
char char_matrix_product[MATRIX_SIZE * MATRIX_SIZE];
int row_1 = 0, col_1 = 0, row_2 = 0, col_2 = 0;

//---------------------User debugging-----------------------
extern int32_t MaxJitter;             // largest time jitter between interrupts in usec

#define PD0  (*((volatile uint32_t *)0x40007004))
#define PD1  (*((volatile uint32_t *)0x40007008))
#define PD2  (*((volatile uint32_t *)0x40007010))
#define PD3  (*((volatile uint32_t *)0x40007020))

void PortD_Init(void){ 
  SYSCTL_RCGCGPIO_R |= 0x08;       // activate port D
  while((SYSCTL_RCGCGPIO_R&0x08)==0){};      
  GPIO_PORTD_DIR_R |= 0x0F;        // make PD3-0 output heartbeats
  GPIO_PORTD_AFSEL_R &= ~0x0F;     // disable alt funct on PD3-0
  GPIO_PORTD_DEN_R |= 0x0F;        // enable digital I/O on PD3-0
  GPIO_PORTD_PCTL_R = ~0x0000FFFF;
  GPIO_PORTD_AMSEL_R &= ~0x0F;;    // disable analog functionality on PD
}


//------------------Idle Task--------------------------------
// foreground thread, runs when nothing else does
// never blocks, never sleeps, never dies
// inputs:  none
// outputs: none
void Idle(void){
  IdleCount = 0;          
  while(1) {
    IdleCount++;
    PD0 ^= 0x01;
    WaitForInterrupt();
  }
}

//--------------end of Idle Task-----------------------------

void RemoteTerminal(void);

void ButtonWork1(void){
  uint32_t myId = OS_Id(); 
  //ST7735_Message(0,1,"SequenceNum = ",sequenceNum); 
  OS_Kill();  // done, OS does not return from a Kill
} 

void multiply_matrices(uint32_t (*matrix1)[MATRIX_SIZE], int rows1, int cols1,
                       uint32_t (*matrix2)[MATRIX_SIZE], int rows2, int cols2,
                       uint32_t (*result_matrix)[MATRIX_SIZE]) {
    if (cols1 != rows2) {
        printf("Error: incompatible matrix sizes\n");
        return;
    }

    // Multiply the matrices
    for (int i = 0; i < rows1; i++) {
        for (int j = 0; j < cols2; j++) {
            result_matrix[i][j] = 0;
            for (int k = 0; k < cols1; k++) {
                result_matrix[i][j] += matrix1[i][k] * matrix2[k][j];
            }
        }
    }
}

char cmd[100];

void RemoteTerminal(void)	{
	
	// ******** Set up server **************////////////
	
	// Initialize and bring up Wifi adapter  
  if(!ESP8266_Init(true,false)) {  // verbose rx echo on UART for debugging
    ST7735_DrawString(0,1,"No Wifi adapter",ST7735_YELLOW); 
    OS_Kill();
  }
  // Get Wifi adapter version (for debugging)
  ESP8266_GetVersionNumber(); 
  // Connect to access point
  if(!ESP8266_Connect(true)) {  
    ST7735_DrawString(0,1,"No Wifi network",ST7735_YELLOW); 
    OS_Kill();
  }
  ST7735_DrawString(0,1,"Wifi connected",ST7735_GREEN);
  if(!ESP8266_StartServer(23,600)) {  // port 80, 5min timeout
    ST7735_DrawString(0,2,"Server failure",ST7735_YELLOW); 
    OS_Kill();
  }  
  ST7735_DrawString(0,2,"Server started",ST7735_GREEN);
	
	// ******** Set up server **************////////////
	
//	TelnetServerID = OS_Id();
	
	//Interpreter_Remote();
	
}







char* parse_matrix(char* arr, int num_rows, int num_cols, uint32_t (*matrix)[MATRIX_SIZE]){ 
	int row = 0, col = 0, num = 0, next_matrix_index = 1, num_elements = 0;
	//printf("arr[4]: %d\n\r", arr[1]);
	for (int i = 1; arr[i] != '\0'; i++) {
		//printf("HERE\n\r");
			if (arr[i] == '[') {
					col = 0;
			} else if (arr[i] == ']') {
					num_elements ++;
					matrix[row][col] = num;
					num = 0;
					row++;
			} else if (arr[i] == ',') {
					num_elements ++;
					matrix[row][col] = num;
					num = 0;
					col++;
			} else if (arr[i] >= '0' && arr[i] <= '9') {
					num = num * 10 + (arr[i] - '0');
			}
			if (row > num_rows){
				break;
			}
			next_matrix_index++;
	}
	//printf("out of loop\n\r");
	//printf("array[next_index]: %d\n\r", next_matrix_index);
	//printf("array[end]: %d\n\r", arr[next_matrix_index + 4]);
	return &arr[next_matrix_index + 4];
}




int parse_indices(char *arr, int buffer_size, int *row_1, int *col_1, int *row_2, int *col_2){
	int num = 0;
	int counter = 0;
	for (int i = 0; i < buffer_size; i++) {
		if (arr[i] >= '0' && arr[i] <= '9') {
			num = num * 10 + (arr[i] - '0');
		} 
		else {
			if (arr[i] == ',' || arr[i] == ']') {
				if(counter == 0){
					*row_1 = num;
				}
				else if(counter == 1){
					*col_1 = num;
				}
				else if(counter == 2){
					*row_2 = num;
				}
				else if(counter == 3){
					*col_2 = num;
					// return index that we are at in the array
					return i + 4;
				}
				counter++;
				num = 0;
			}
		}
	}
	
	return 0;
}





void print_matrix(int rows, int cols, uint32_t (*matrix)[MATRIX_SIZE]){
	 // print matrix
    printf("Parsed matrix:\n\r");
    for (int i = 0; i < rows; i++) {
			for (int j = 0; j < cols; j++) {
					printf("%d ", matrix[i][j]);
			}
			printf("\n\r");
    }
}

void parse_data(char *arr, int buffer_size, int *row_1, int *col_1, int *row_2, int *col_2, uint32_t (*matrix_1)[MATRIX_SIZE], uint32_t (*matrix_2)[MATRIX_SIZE]){
	// get indices of all matrices
	int begin_matrix_1 = parse_indices(arr, buffer_size, row_1, col_1, row_2, col_2);
	
	//printf("row_1: %d col_1: %d row_2: %d col_2: %d\n\r", *row_1, *col_1, *row_2, *col_2);	// Total numnber of rows and columns for both matrices
	//printf("arr[begin_matrix_1] = %d\n\r", arr[begin_matrix_1]);	// This is the first element of matrix 1
	
	// parse matrix 1
	char* begin_matrix_2 = parse_matrix(&arr[begin_matrix_1], *row_1, *col_1, matrix_1);
	printf("rows: %d, cols: %d\n\r", *row_1, *col_1);
	//print_matrix(*row_1,*col_1, matrix_1);
	printf("Beginning element of next matrix: %d\n\r", *begin_matrix_2);

	// parse matrix 2
	char* end_of_matrix_2 = parse_matrix(begin_matrix_2, *row_2, *col_2, matrix_2);
	printf("rows: %d, cols: %d\n\r", *row_2, *col_2);
	//print_matrix(*row_2, *col_2, matrix_2);
}


void matrix_to_char_array(uint32_t (*matrix)[MATRIX_SIZE], size_t rows, size_t cols, char *result) {
    // Iterate over each row and column of the matrix
    for (size_t i = 0; i < rows; i++) {
        for (size_t j = 0; j < cols; j++) {
            // Convert the current matrix element to a char array
            char element[5];
            sprintf(element, "%u ", matrix[i][j]);
            // Copy the char array to the result array
            strcat(result, element);
        }
        // Add a newline character at the end of each row
        strcat(result, "\n");
    }
}

void ProcessTasks(void){
	
	char status_str[20];                     // a character array to store the converted string
	strcpy(status_str, buffer);              // copy the contents of c_str into str
	printf("%s\n", status_str);             // print the converted string
	
	if(strcmp(status_str, "no tasks") != 0){ // we have tasks to complete
		parse_data(buffer, BUFFER_SIZE, &row_1, &col_1, &row_2, &col_2, matrix_1, matrix_2);
	
		// multiply matrices
		multiply_matrices(matrix_1, row_1, col_1, matrix_2, row_2, col_2, product_matrix);

		print_matrix(row_1, col_2, product_matrix);

		matrix_to_char_array(product_matrix, row_1, col_2, char_matrix_product);

		// send result matrix back to server
		if(ESP8266_Send(char_matrix_product)){
			printf("Result matrix sent\n\r");
		}
	}

}


void SendHeartbeat(void){
	if(ESP8266_Send("heartbeat")){
		printf("Sent hearbeat to server\n\r");
	}
}

void ClearMatrixProduct(void){
	for(int i = 0; i < MATRIX_SIZE * MATRIX_SIZE; i++){
		char_matrix_product[i] = ' ';
	}
}

void ClearBuffer(void){
	for(int i = 0; i < BUFFER_SIZE; i++){
		buffer[i] = ' ';
	}
}
void ConnectWifiClient(void){
	ST7735_DrawString(1,5,"Main Client",ST7735_MAGENTA); 
	
  // Initialize and bring up Wifi adapter  
  if(!ESP8266_Init(true,false)) {  // verbose rx echo on UART for debugging
    ST7735_DrawString(0,1,"No Wifi adapter",ST7735_YELLOW); 
    OS_Kill();
  }
  // Get Wifi adapter version (for debugging)
  ESP8266_GetVersionNumber(); 
  // Connect to access point
  if(!ESP8266_Connect(true)) {  
    ST7735_DrawString(0,1,"No Wifi network",ST7735_YELLOW); 
    OS_Kill();
  }
  ST7735_DrawString(0,1,"Wifi connected",ST7735_GREEN);
	
	if(!ESP8266_MakeTCPConnection("10.159.64.223", 6000, 0, false)){
	//if(!ESP8266_MakeTCPConnection("LAPTOP-OCK1V7QT", 6000, 0, false)){ 
    ST7735_DrawString(0,2,"Connection failed",ST7735_YELLOW); 
    OS_Kill();
  } 
	
	// Send the string "task" to the server
	if(ESP8266_Send("task")){
		printf("Told server we are ready\n\r");
	}
	
	// Send heartbeat
	//SendHeartbeat();
	
	char status_str[20];                     // a character array to store the converted string
	

	while(strcmp(status_str, "no tasks") != 0){ // we have tasks to complete

		// Get data from server
		ESP8266_Receive(buffer, BUFFER_SIZE);
		
		strcpy(status_str, buffer);              // copy the contents of c_str into str
		
		// make sure server didn't say to shut down
		if(strcmp(status_str, "shut down") == 0){
			break;
		}
	
		printf("Processing tasks\n\r");
		parse_data(buffer, BUFFER_SIZE, &row_1, &col_1, &row_2, &col_2, matrix_1, matrix_2);
		
		SendHeartbeat();

		// multiply matrices
		multiply_matrices(matrix_1, row_1, col_1, matrix_2, row_2, col_2, product_matrix);
		
		SendHeartbeat();

		//print_matrix(row_1, col_2, product_matrix);

		matrix_to_char_array(product_matrix, row_1, col_2, char_matrix_product);
		
		// send that we are done
		if(ESP8266_Send("done")){
			printf("Told server we are done\n\r");
		}
		// send result matrix back to server
		if(ESP8266_Send(char_matrix_product)){
			printf("Result matrix sent\n\r");
		}
		
		ClearMatrixProduct();
		ClearBuffer();		
		
		// get another task
		if(ESP8266_Send("task")){
			printf("Requested another task\n\r");
		}
	}
	
	
  OS_Kill(); 
} 

void ConnectWifiBackup(void){
	
	ST7735_DrawString(1,5,"Backup Client",ST7735_MAGENTA); 
	
  // Initialize and bring up Wifi adapter  
  if(!ESP8266_Init(true,false)) {  // verbose rx echo on UART for debugging
    ST7735_DrawString(0,1,"No Wifi adapter",ST7735_YELLOW); 
    OS_Kill();
  }
  // Get Wifi adapter version (for debugging)
  ESP8266_GetVersionNumber(); 
  // Connect to access point
  if(!ESP8266_Connect(true)) {  
    ST7735_DrawString(0,1,"No Wifi network",ST7735_YELLOW); 
    OS_Kill();
  }
  ST7735_DrawString(0,1,"Wifi connected",ST7735_GREEN);
	
	if(!ESP8266_MakeTCPConnection("10.159.64.223", 6000, 0, false)){
	//if(!ESP8266_MakeTCPConnection("LAPTOP-OCK1V7QT", 6000, 0, false)){ 
    ST7735_DrawString(0,2,"Connection failed",ST7735_YELLOW); 
    OS_Kill();
  } 
	int count = 0;
	char activate_message[30];
	// Wait for activate
	ESP8266_Receive(buffer, 8);
	printf("Received\n\r");
	strcpy(activate_message, buffer);              // see if activate message was received

	
	if(strcmp(activate_message, "activate") != 0){
		printf("Backup has been activated!\n\r");
	}
	
	// Send the string "task" to the server
	if(ESP8266_Send("task")){
		printf("Told server we are ready\n\r");
	}
	
	char status_str[20];                     // a character array to store the converted string
	

	while(strcmp(status_str, "no tasks") != 0){ // we have tasks to complete

		// Get data from server
		ESP8266_Receive(buffer, BUFFER_SIZE);
		
		strcpy(status_str, buffer);              // copy the contents of c_str into str
	
		printf("Processing tasks\n\r");
		parse_data(buffer, BUFFER_SIZE, &row_1, &col_1, &row_2, &col_2, matrix_1, matrix_2);
		
		SendHeartbeat();

		// multiply matrices
		multiply_matrices(matrix_1, row_1, col_1, matrix_2, row_2, col_2, product_matrix);
		
		SendHeartbeat();

		//print_matrix(row_1, col_2, product_matrix);

		matrix_to_char_array(product_matrix, row_1, col_2, char_matrix_product);
		
		// send that we are done
		if(ESP8266_Send("done")){
			printf("Told server we are done\n\r");
		}
		// send result matrix back to server
		if(ESP8266_Send(char_matrix_product)){
			printf("Result matrix sent\n\r");
		}
		
		ClearMatrixProduct();
		ClearBuffer();		
		
		// get another task
		if(ESP8266_Send("task")){
			printf("Requested another task\n\r");
		}
	}
	
	
  OS_Kill(); 
} 


void SW1Push2(void){
	
	parse_data(buffer, BUFFER_SIZE, &row_1, &col_1, &row_2, &col_2, matrix_1, matrix_2);
	
	// multiply matrices
	multiply_matrices(matrix_1, row_1, col_1, matrix_2, row_2, col_2, product_matrix);
	
	print_matrix(row_1, col_2, product_matrix);
	
	matrix_to_char_array(product_matrix, row_1, col_2, char_matrix_product);
	
	// send result matrix back to server
	if(ESP8266_Send(char_matrix_product)){
		printf("Result matrix sent\n\r");
	}
	/*
	for(int i = 0; i < row_1; i++){
		for(int j = 0; j < col_1; j++){
			printf("matrix_1[%d][%d]: %d\n\r", i, j, matrix_1[i][j]);
		}
	}
	
	for(int i = 0; i < row_2; i++){
		for(int j = 0; j < col_2; j++){
			printf("matrix_2[%d][%d]: %d\n\r", i, j, matrix_2[i][j]);
		}
	}
	*/
}

int StartClient(void){  
  OS_Init();           // initialize, disable interrupts
  PortD_Init();
  
	OS_AddSW1Task(&SW1Push2,2);
  
  // create initial foreground threads
  NumCreated = 0;
  NumCreated += OS_AddThread(&Idle,128,4); 
  NumCreated += OS_AddThread(&ConnectWifiClient,128,3); 
 
  OS_Launch(10*TIME_1MS); // doesn't return, interrupts enabled in here
  return 0;               // this never executes
}

int StartBackup(void){
	OS_Init();           // initialize, disable interrupts
  PortD_Init();
  
	OS_AddSW1Task(&SW1Push2,2);
  
  // create initial foreground threads
  NumCreated = 0;
  NumCreated += OS_AddThread(&Idle,128,4); 
  NumCreated += OS_AddThread(&ConnectWifiBackup,128,3); 
 
  OS_Launch(10*TIME_1MS); // doesn't return, interrupts enabled in here
  return 0;               // this never executes
}


//*******************Trampoline for selecting main to execute**********
int main(void) { 			// main

	//StartClient();
	StartBackup();
}
