// *************os.c**************
// EE445M/EE380L.6 Labs 1, 2, 3, and 4 
// High-level OS functions
// Students will implement these functions as part of Lab
// Runs on LM4F120/TM4C123
// Jonathan W. Valvano 
// Jan 12, 2020, valvano@mail.utexas.edu

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../inc/tm4c123gh6pm.h"
#include "../inc/CortexM.h"
#include "../inc/PLL.h"
#include "../inc/LaunchPad.h"
#include "../inc/WTimer0A.h"
#include "../RTOS_Labs_common/OS.h"
#include "../RTOS_Labs_common/ST7735.h"
#include "../inc/ADCT0ATrigger.h"
#include "../RTOS_Labs_common/UART0int.h"
#include "../RTOS_Labs_common/eFile.h"
#include "../RTOS_Labs_common/UART0int.h"
#include "../inc/Timer4A.h"
#include "../inc/Timer2A.h"
#include "../inc/Timer3A.h"

//====++++==== Define Statements ====++++====
//#define EN_DEBUG_ASSERTS
#define JITTERSIZE 256
#define PD1  (*((volatile uint32_t *)0x40007008))
#define PD0  (*((volatile uint32_t *)0x40007004))
// Define statements for Lab 3
#define ROUNDROBIN      // Run round robin scehduler 
//#define PRIORITY		  // Run priority scheduler
//#define SPINLOCK			  // spinlock semaphores
//#define COOPERATIVE 	// cooperative semaphore
#define BLOCKING			// blocking semaphore

#ifdef EN_DEBUG_ASSERTS
  enum OS_ERROR_TYPE {
    OS_SOFC=0,                        //Stack Overflow count
    OS_SUFC=1,                        //Stack Underflow count
    OS_ATFC=2,                        //Add Thread Fail count
    OS_APTFC=3,                       //Add Periodic Thread Fail Count
    OS_Misc_E=4,
    OS_ETYPE_COUNT
  };
#endif

//====++++ Assembly Imports & Function Def ++++====
void StartOS(void);
void ContextSwitch(void);

void OS_Sleep_List_Pop(int32_t elapsed_time_ms);

//====++++====++++ OS Data  ++++====++++====
typedef struct OS_DATA {
  TCBPtr TCB_RunPt;                           // This is the TCB for the currently running task, see osasm.s for useage
  TCBPtr TCB_Jump;                            // This allows Jumps to diffrent elements of the TCB
  TCBPtr TCB_Head;                            // this is head of the linked list for the TCB
  TCBPtr TCB_Tail;
    
  #ifdef EN_DEBUG_ASSERTS
    uint32_t ERROR_COUNT;                     //Generalized Error Counter
    uint8_t  ERROR_ARR[OS_ETYPE_COUNT];       //Errors sorted by type                 
    uint32_t ASSERT_GUARD_0;     //Overflow Guard for Debug Asserts
  #endif
  uint32_t StackPool[TCB_NUM][STACK_SIZE];
  #ifdef EN_DEBUG_ASSERTS              
    uint32_t ASSERT_GUARD_1;  //Overflow Guard for Debug Asserts
  #endif
  TCBType TCBPool[TCB_NUM];                   // list of TCBs 
  #ifdef EN_DEBUG_ASSERTS              
    uint32_t ASSERT_GUARD_2;  //Overflow Guard for Debug Asserts
  #endif
  uint32_t SYSTIME_MS;
  uint32_t SYSTIME_Slice_MS;
  #ifdef EN_DEBUG_ASSERTS              
    uint32_t ASSERT_GUARD_3;  //Overflow Guard for Debug Asserts
  #endif
  uint8_t   PT_Count;
  //uint32_t  PT_Last_Delay;
  //uint32_t  PT_Next_Delay;
  void     (*PT_Tasks[4])(void);
  //uint32_t  PT_Period[4];
  //uint32_t  PT_Remaining[4];
  //uint8_t   PT_Priority[4];
  #ifdef EN_DEBUG_ASSERTS              
    uint32_t ASSERT_GUARD_4;  //Overflow Guard for Debug Asserts
  #endif
  TCBPtr Sleep_List_Head;
  TCBPtr Sleep_List_Tail;
  TCBPtr Block_List_Head;
  TCBPtr Block_List_Tail;
  #ifdef EN_DEBUG_ASSERTS              
    uint32_t ASSERT_GUARD_5;  //Overflow Guard for Debug Asserts
  #endif
  
  // Performance Measurements 
  uint32_t LastTime[2];    
  uint32_t MaxJitter[2];
  uint32_t JitterHistogram[2][JITTERSIZE];
  
  #ifdef EN_DEBUG_PROFILING
	uint32_t inCriticalMax;
  uint32_t inCritical;
  uint32_t outCritical;
  uint32_t ltime;
  #endif
  
  
} OS_DATA;

OS_DATA OS;

// Mailbox Variables
uint32_t Mailbox;     // contains data passed from producer to consumer
Sema4Type BoxFree;    // indicates whether mailbox is empty (1) or full (0)
Sema4Type DataValid;  // indicates whether the the mailbox has new data that has not been picked up yet (1)

// Fifo Variables
#define MAXFIFO	256				  // Max FIFO size
uint32_t volatile *PutPt;         // Put next
uint32_t volatile *GetPt;         // Get next
uint32_t Fifo[MAXFIFO];			  // array with fifo data
uint32_t static FifoSize;		  // size of FIFO
Sema4Type CurrentSize;            // 0 means FIFO empty
Sema4Type RoomLeft;               // 0 means FIFO is full
Sema4Type FIFOMutex;              // exclusive access to FIFO

//====++++==== Basic Functions ====++++====
#ifdef EN_DEBUG_ASSERTS
  void OS_Error(enum OS_ERROR_TYPE error_type){
    OS.ERROR_ARR[error_type]++;
    OS.ERROR_COUNT++;                         //Used to create a single break point location
  }
  uint8_t OS_Check_Guards(){
    if (OS.ASSERT_GUARD_0 != 0x42424242) {OS_Error(OS_SOFC); OS.ASSERT_GUARD_0 = 0x42424242; return 1;}
    if (OS.ASSERT_GUARD_1 != 0x42424242) {OS_Error(OS_SOFC); OS.ASSERT_GUARD_1 = 0x42424242; return 1;}
    if (OS.ASSERT_GUARD_2 != 0x42424242) {OS_Error(OS_SOFC); OS.ASSERT_GUARD_2 = 0x42424242; return 1;}
    if (OS.ASSERT_GUARD_3 != 0x42424242) {OS_Error(OS_SOFC); OS.ASSERT_GUARD_3 = 0x42424242; return 1;}
    if (OS.ASSERT_GUARD_4 != 0x42424242) {OS_Error(OS_SOFC); OS.ASSERT_GUARD_4 = 0x42424242; return 1;}
    if (OS.ASSERT_GUARD_5 != 0x42424242) {OS_Error(OS_SOFC); OS.ASSERT_GUARD_5 = 0x42424242; return 1;}
    return 0;
  }
#endif

long OS_SCritical(){
    long sr = StartCritical();
    #ifdef EN_DEBUG_PROFILING
        uint32_t now = OS_Time();
        if(now<0xF0000000){
            if(sr){     
                uint32_t inC =  OS_TimeDifference(OS.ltime, now);
                OS.inCritical  += inC;
                if (inC > OS.inCriticalMax){
                    OS.inCriticalMax = inC;
                }
            } else {    OS.outCritical += OS_TimeDifference(OS.ltime, now); }
            OS.ltime = OS_Time();
        }
    #endif
    return sr;
}

void OS_ResetMetrics(){
	OS.inCritical = 0;
	OS.inCriticalMax = 0;
	OS.outCritical = 0;
	OS.ltime = 0;
	OS_ClearMsTime();
}

void OS_ECritical(long sr){
    #ifdef EN_DEBUG_PROFILING
        uint32_t now = OS_Time();
        if(now<0xF0000000){
            if(sr){     
                uint32_t inC =  OS_TimeDifference(OS.ltime, now);
                OS.inCritical  += inC;
                if (inC > OS.inCriticalMax){
                    OS.inCriticalMax = inC;
                }
            } else {    OS.outCritical += OS_TimeDifference(OS.ltime, now); }
            OS.ltime = OS_Time();
        }
    #endif
    EndCritical(sr);
}

void OS_Report_Critical(uint32_t *inCritical, uint32_t *outCritical, uint32_t *inCriticalMax){
    #ifdef EN_DEBUG_PROFILING
        *inCritical  = OS.inCritical;
        *outCritical = OS.outCritical;
        *inCriticalMax = OS.inCriticalMax;
    #else
        *inCritical  = 0;
        *outCritical = 0;
        *inCriticalMax = 0;
    #endif
}


void OS_Init_Data(){
  #ifdef EN_DEBUG_ASSERTS
    OS.ERROR_COUNT = 0;
    for(int i=0; i<OS.ERROR_COUNT; i++){OS.ERROR_ARR[i]=0;}
    OS.ASSERT_GUARD_0 = 0x42424242;
    OS.ASSERT_GUARD_1 = 0x42424242;
    OS.ASSERT_GUARD_2 = 0x42424242;
    OS.ASSERT_GUARD_3 = 0x42424242;
    OS.ASSERT_GUARD_4 = 0x42424242;
    OS.ASSERT_GUARD_5 = 0x42424242;
    OS_Check_Guards; //So if we forget to init, it yells at the start instead of giving false positives
  #endif  
  OS.TCB_Jump = NULL;
  
  //Initialize the the TCBs to be inactive
  for(int i = 0; i < TCB_NUM; i++){
    OS.TCBPool[i].next_tcb = NULL;
    OS.TCBPool[i].sleep_state = -1;
    OS.TCBPool[i].blocked_state = -1; 
  }
  
  OS.SYSTIME_MS = 0;
  
  OS.PT_Count = 0;
  /*
  OS.PT_Last_Delay  = 0;
  OS.PT_Next_Delay  = 0;
  for(int i=0; i<TCB_NUM; i++){
    OS.PT_Period[i]    = 0;
    OS.PT_Remaining[i] = 0;
  }*/
  
  OS.Sleep_List_Head = NULL;
  OS.Sleep_List_Tail = NULL;
  OS.Block_List_Head = NULL;
  OS.Block_List_Tail = NULL;
  
  for(int i=0; i<2;i++){
    OS.MaxJitter[i] = 0;
    OS.LastTime[i] = 0;
    for(int j=0; j<JITTERSIZE;j++){
        OS.JitterHistogram[i][j]=0;
    }
  }
  
  #ifdef EN_DEBUG_PROFILING
  OS.inCritical = 0;
	OS.inCriticalMax = 0;
  OS.outCritical = 0 ;
  OS.ltime = 0;
  #endif
}


//====++++==== OS Functions ====++++====
	
// ******** OS_Record_Jitter ************
// Jitter Calculations
// input:  Periodic Task Index, Period 
void OS_Record_Jitter( uint8_t PTIndex, uint32_t period){ 
  uint32_t cTime = OS_Time();
  if( OS.LastTime[PTIndex] != 0 && cTime > 80000 ){
      uint32_t eTime = OS_TimeDifference(OS.LastTime[PTIndex], cTime);
      eTime = eTime > period ? eTime-period : period-eTime;
      eTime = eTime;
      OS.MaxJitter[PTIndex] = eTime > OS.MaxJitter[PTIndex] ? eTime : OS.MaxJitter[PTIndex];
      eTime = eTime >= JITTERSIZE ? JITTERSIZE-1 : eTime; 
      OS.JitterHistogram[PTIndex][eTime]++;
  }
  OS.LastTime[PTIndex] = cTime;
}

uint32_t* OS_Report_Jitter( uint8_t PTIndex, uint32_t* MaxJitter){
    *MaxJitter = OS.MaxJitter[PTIndex];
    return (OS.JitterHistogram[PTIndex]);
}

//Bypasses a given TCB in linked list Such that:
//OLD Prev -> toBypass -> Next
//New Prev -> Next and toBypass -> Next and OS.TCB_Head != toBypass != OS.TCB_Tail
void OS_TCB_Bypass( TCBPtr toBypass ){
  TCBPtr Prev,Next;
  Prev = OS.TCB_Tail;
  while( Prev->next_tcb != toBypass ){ Prev = Prev->next_tcb; } // gets item before RunPt
  Next = Prev->next_tcb->next_tcb; // gets item after RunPt
  Prev->next_tcb = Next; // skips RunPt in list to connect item before and after RunPt
  if(toBypass == OS.TCB_Head){ OS.TCB_Head = Next; }
  if(toBypass == OS.TCB_Tail){ OS.TCB_Tail = Prev; }
}

// ******** OS_Return_TCB ************
// Adds an inactive TCB to the TCB Rotation
void OS_Return_TCB(TCBPtr inactiveTCB ){
    TCBPtr prev, next; 
    long sr;
    inactiveTCB->blocked_state = -1;
    inactiveTCB->sleep_state = -1;

    sr = OS_SCritical();
    
    if (OS.TCB_Head->priority <= inactiveTCB->priority){
        inactiveTCB->next_tcb = OS.TCB_Head;
        OS.TCB_Tail->next_tcb = inactiveTCB;
        OS.TCB_Head = inactiveTCB;
    } else if ( inactiveTCB->priority <= inactiveTCB->priority) {
        inactiveTCB->next_tcb = OS.TCB_Head;
        OS.TCB_Tail->next_tcb = inactiveTCB;
        OS.TCB_Tail = inactiveTCB;
    } else {
        prev = OS.TCB_Tail;
        while( prev->next_tcb->priority < inactiveTCB->priority ){ prev = prev->next_tcb; } // gets item before RunPt
        next = prev->next_tcb; 
        prev->next_tcb = inactiveTCB;
        inactiveTCB->next_tcb = next;
    }
    OS_ECritical(sr);
}

// ******** OS_Scheduler ************
void OS_Scheduler(void){
	
	#ifdef PRIORITY
	uint8_t head_pri = OS.TCB_Head->priority;
	uint8_t my_pri = OS.TCB_RunPt->priority;
	uint8_t next_pri = OS.TCB_RunPt->next_tcb->priority;
	
	if( head_pri < my_pri || next_pri > my_pri){
			//Return to top if there is more important work, or if next work is low priority
			OS.TCB_Jump = OS.TCB_Head;
	} 
	ContextSwitch();
	#endif
	
	#ifdef ROUNDROBIN
	ContextSwitch();
	#endif
}


// ******** SysTick_Init ************
// Intitalized Systick timer, used to control context switching
void SysTick_Init(unsigned long period){
  NVIC_ST_CTRL_R = 0;         // Disable SysTick timer
  NVIC_ST_RELOAD_R = period-1;// Set reload value
  NVIC_ST_CURRENT_R = 0;      // Reset systick to max via write
  NVIC_SYS_PRI3_R = (NVIC_SYS_PRI3_R&0x00FFFFFF)|0xC0000000; // priority 6 (Higher than PendSV, lower than all else)
  NVIC_ST_CTRL_R = 0x07;      // Enable SysTick with core clock and interrupts
}

#define Systick_Stop()     do{NVIC_ST_CTRL_R = 0;   }while(0)
#define Systick_Continue() do{NVIC_ST_CTRL_R = 0x7; }while(0)
#define Systick_Restart()  do{NVIC_ST_CURRENT_R = 0; NVIC_ST_CTRL_R = 0x07;}while(0) 
                    
void SysTick_Handler(void) {
  #ifdef EN_DEBUG_ASSERTS
    if ( OS_Check_Guards() ){
      //I have corrupted OS memory ;-;
    } else if( OS.TCB_RunPt->id > 0 && OS.TCBPool[OS.TCB_RunPt->id-1].next_tcb != NULL && 0x42424242 != OS.StackPool[ OS.TCB_RunPt->id-1][STACK_SIZE-1]){
      //I have corrupted a neighbors stack
      OS_Error(OS_SOFC);      
      OS.StackPool[ OS.TCB_RunPt->id-1][STACK_SIZE-1] = 0x42424242;
    } else if( 0x42424242 != OS.StackPool[ OS.TCB_RunPt->id][STACK_SIZE-1]){
      //I have corrupted my own stack
      OS_Error(OS_SUFC);      
      OS.StackPool[ OS.TCB_RunPt->id][STACK_SIZE-1] = 0x42424242;
    } 
  #endif
  OS_Sleep_List_Pop( OS.SYSTIME_Slice_MS );
  OS_Scheduler();
}


unsigned long OS_LockScheduler(void){
  // lab 4 might need this for disk formating
  return 0;// replace with solution
}
void OS_UnLockScheduler(unsigned long previous){
  // lab 4 might need this for disk formating
}


/**
 * @details  Initialize operating system, disable interrupts until OS_Launch.
 * Initialize OS controlled I/O: serial, ADC, systick, LaunchPad I/O and timers.
 * Interrupts not yet enabled.
 * @param  none
 * @return none
 * @brief  Initialize OS
 */
void OS_Init(void){
  DisableInterrupts();
  PLL_Init(Bus80MHz);
  OS_Init_Data();
  UART_Init();                // serial I/O for interpreter
  ST7735_InitR(INITR_REDTAB); // LCD initialization
  OS_ClearMsTime();
  LaunchPad_Init();

  //Set up PendSV with the lowest possible priority (Highest value (7) is lowest priority)
  NVIC_SYS_PRI3_R |= (NVIC_SYS_PRI3_R&0xFF00FFFF)|0x00E00000 ; //pendSV bits are 23-21
  
  //ST7735_Message(0,0,"OS Init done.", 0);
}; 

// ******** OS_InitSemaphore ************
// initialize semaphore 
// input:  pointer to a semaphore
void OS_InitSemaphore(Sema4Type *semaPt, int32_t value){
  semaPt->Value = value;
}; 

// ******** OS_Wait ************
// decrement semaphore 
// Lab2 spinlock
// Lab3 block if less than zero
// input:  pointer to a counting semaphore
void OS_Wait(Sema4Type *semaPt){
	#ifdef SPINLOCK
	DisableInterrupts();
	while(semaPt->Value <= 0){
		EnableInterrupts();
		DisableInterrupts();
	}
	semaPt->Value = semaPt->Value-1;
	EnableInterrupts();
	#endif
	
	#ifdef COOPERATIVE
	DisableInterrupts();
	while(semaPt->Value <= 0){
		EnableInterrupts();
		OS_Suspend(); 				// run thread switcher
		DisableInterrupts();
	}
	semaPt->Value = semaPt->Value-1;
	EnableInterrupts();
	#endif
	
	#ifdef BLOCKING
	long sr = OS_SCritical();
	
	semaPt->Value = semaPt->Value - 1;
	if(semaPt->Value < 0){
        
        OS.TCB_Jump = OS.TCB_RunPt->next_tcb; 
        OS_TCB_Bypass(OS.TCB_RunPt);
		OS.TCB_RunPt->blocked_state = (int32_t)(semaPt); // reason it is blocked
		
        if(OS.Block_List_Head == NULL){
            OS.Block_List_Head = OS.TCB_RunPt;
            OS.Block_List_Tail = OS.TCB_RunPt;
        } else {
            OS.Block_List_Tail->next_tcb = OS.TCB_RunPt;
            OS.Block_List_Tail = OS.TCB_RunPt;
        }
        
        Systick_Restart();
        OS_ECritical(sr);
        ContextSwitch(); 
	} else {
      OS_ECritical(sr);
    }
		#endif
	
}; 

// ******** OS_Signal ************
// increment semaphore 
// Lab2 spinlock
// Lab3 wakeup blocked thread if appropriate 
// input:  pointer to a counting semaphore
void OS_Signal(Sema4Type *semaPt){
	#ifdef SPINLOCK 
  DisableInterrupts();
  semaPt->Value = semaPt->Value + 1;
  EnableInterrupts();
	#endif 
	
	#ifdef COOPERATIVE
  DisableInterrupts();
  semaPt->Value = semaPt->Value + 1;
  EnableInterrupts();
	#endif 
	
	#ifdef BLOCKING
	long sr = OS_SCritical(); 
	
	semaPt->Value = semaPt->Value + 1;
	if(semaPt->Value <= 0){
        TCBPtr prev, next;
        
        prev = OS.Block_List_Head;
        if( OS.Block_List_Head->blocked_state == (int32_t)semaPt){
            if(OS.Block_List_Head == OS.Block_List_Tail) { 
                OS.Block_List_Head = NULL; OS.Block_List_Tail = NULL;
            } else {
                OS.Block_List_Head = OS.Block_List_Head->next_tcb;
            }
            OS_Return_TCB(prev);
        } else {
            do{
                if( prev->next_tcb->blocked_state == (int32_t)semaPt){
                    next = prev->next_tcb;
                    if(next == OS.Block_List_Tail) { 
                        OS.Block_List_Tail = prev;
                    } else {
                        prev->next_tcb = next->next_tcb;
                    }
                    OS_Return_TCB(next);
                    break;
                }    
                prev=prev->next_tcb;
            } while ( prev != OS.Block_List_Tail);
        }
        
	}
    OS_ECritical(sr);
	#endif
    
	
}; 

// ******** OS_bWait ************
// Lab2 spinlock, set to 0
// Lab3 block if less than zero
// input:  pointer to a binary semaphore
void OS_bWait(Sema4Type *semaPt){
	// TODO: Do we change binary semaphores to blocking?
    OS_Wait(semaPt);
    /*
	DisableInterrupts();
	while(semaPt->Value == 0){
		EnableInterrupts();
		DisableInterrupts();
	}
	semaPt->Value = 0;
	EnableInterrupts();*/
}; 

// ******** OS_bSignal ************
// Lab2 spinlock, set to 1
// Lab3 wakeup blocked thread if appropriate 
// input:  pointer to a binary semaphore
// output: none
void OS_bSignal(Sema4Type *semaPt){
  // TODO: Do we change binary semaphores to blocking?
  OS_Signal(semaPt);
  /*long sr = OS_SCritical();
  semaPt->Value = 1;
  OS_ECritical(sr);*/
}; 



//******** OS_AddThread *************** 
// add a foregound thread to the scheduler
// Inputs: pointer to a void/void foreground task
//         number of bytes allocated for its stack
//         priority, 0 is highest, 5 is the lowest
// Outputs: 1 if successful, 0 if this thread can not be added
// stack size must be divisable by 8 (aligned to double word boundary)
// In Lab 2, you can ignore both the stackSize and priority fields
// In Lab 3, you can ignore the stackSize fields
int OS_AddThread(void(*task)(void), uint32_t stackSize, uint32_t priority){
  TCBPtr newNode;
  long sr;
    
  sr = OS_SCritical();
  // node can't be added if stack size is not divisible by 8
  //if(stackSize % 8) { return 0; }	// MT- commented this out so lab 2 will work

  for(int i = 0; i <= TCB_NUM; i++){
    if(i==TCB_NUM) {
        #ifdef EN_DEBUG_ASSERTS
            OS_Error(OS_ATFC);
        #endif
        OS_ECritical(sr); 
        return 0;
    } else if(OS.TCBPool[i].next_tcb == NULL && OS.TCBPool[i].sleep_state == -1 && OS.TCBPool[i].blocked_state == -1){
      newNode = &OS.TCBPool[i];
      newNode->id = i;  // thread ID is index into OS.TCBPool  			
      newNode->curr_stack_ptr = (int32_t)(&OS.StackPool[i][STACK_SIZE-17]); // thread stack pointer    
      OS.StackPool[i][STACK_SIZE-1] = 0x42424242;    // Stack overflow guard
      OS.StackPool[i][STACK_SIZE-2] = 0x01000000;    // thumb bit
      OS.StackPool[i][STACK_SIZE-3] = (int32_t)task; // PC    
      break;
    }
  } 
  
  newNode->priority = priority;
  if(OS.TCB_Head == NULL){
    newNode->sleep_state = -1;
    newNode->blocked_state = -1;
    newNode->next_tcb = newNode;
    OS.TCB_RunPt = newNode;
    OS.TCB_Head = newNode;
    OS.TCB_Tail = newNode;
  } else {
    OS_Return_TCB(newNode);
  }
  
  OS_ECritical(sr);
  return 1; // MT fixed this because returning 1 means it was successful
};

//******** OS_AddProcess *************** 
// add a process with foregound thread to the scheduler
// Inputs: pointer to a void/void entry point
//         pointer to process text (code) segment
//         pointer to process data segment
//         number of bytes allocated for its stack
//         priority (0 is highest)
// Outputs: 1 if successful, 0 if this process can not be added
// This function will be needed for Lab 5
// In Labs 2-4, this function can be ignored
int OS_AddProcess(void(*entry)(void), void *text, void *data, 
  unsigned long stackSize, unsigned long priority){
  // put Lab 5 solution here

     
  return 0; // replace this line with Lab 5 solution
}


//******** OS_Id *************** 
// returns the thread ID for the currently running thread
// Inputs: none
// Outputs: Thread ID, number greater than zero 
uint32_t OS_Id(void){
  return OS.TCB_RunPt->id;
};



//******** OS_AddPeriodicThread *************** 
// add a background periodic task
// typically this function receives the highest priority
// Inputs: pointer to a void/void background function
//         period given in system time units (12.5ns)
//         priority 0 is the highest, 5 is the lowest
// Outputs: 1 if successful, 0 if this thread can not be added
// You are free to select the time resolution for this function
// It is assumed that the user task will run to completion and return
// This task can not spin, block, loop, sleep, or kill
// This task can call OS_Signal  OS_bSignal   OS_AddThread
// This task does not have a Thread ID
// In lab 1, this command will be called 1 time
// In lab 2, this command will be called 0 or 1 times
// In lab 2, the priority field can be ignored
// In lab 3, this command will be called 0 1 or 2 times
// In lab 3, there will be up to four background threads, and this priority field 
//           determines the relative priority of these four threads
int OS_AddPeriodicThread(void(*task)(void), uint32_t period, uint32_t priority){
  if( OS.PT_Count >= 4 ) { //Fail if no space
      #ifdef EN_DEBUG_ASSERTS
        OS_Error(OS_APTFC);
      #endif
      return 0;
  }    
  if(OS.PT_Count == 0 ){ Timer4A_Init(task,period,priority); OS.PT_Count++;}
  else if (OS.PT_Count == 1) {Timer2A_Init(task,period,priority); OS.PT_Count++;}
  else if(OS.PT_Count == 2) {Timer3A_Init(task,period,priority); OS.PT_Count++;}
  
  return 1; 
};


/*----------------------------------------------------------------------------
  PF1 Interrupt Handler
 *----------------------------------------------------------------------------*/
void SWOneInit(void){
  unsigned long volatile delay;
  SYSCTL_RCGCGPIO_R |= 0x00000020; // (a) activate clock for port F
  delay = SYSCTL_RCGCGPIO_R;
  GPIO_PORTF_CR_R = 0x10;         // allow changes to PF4,0
  GPIO_PORTF_DIR_R &= ~0x10;    // (c) make PF4,0 in (built-in button)
  GPIO_PORTF_AFSEL_R &= ~0x10;  //     disable alt funct on PF4,0
  GPIO_PORTF_DEN_R |= 0x10;     //     enable digital I/O on PF4,0
  GPIO_PORTF_PCTL_R &= ~0;    //     PF4,PF0 is not both edges
  GPIO_PORTF_IEV_R &= ~0x10;    //     PF4,PF0 falling edge event
  GPIO_PORTF_ICR_R = 0x10; //  configure PF4,0 as GPIO
  GPIO_PORTF_AMSEL_R &= ~0x10;  //     disable analog functionality on PF4,0
  GPIO_PORTF_PUR_R |= 0x10;     //     enable weak pull-up on PF4,0
  GPIO_PORTF_IS_R &= ~0x10;     // (d) PF4,PF0 is edge-sensitive
  GPIO_PORTF_IBE_R |= 0x10;      // (e) clear flags 4,0
  GPIO_PORTF_IM_R |= 0x10;      // (f) arm interrupt on PF4,PF0

  NVIC_PRI7_R = (NVIC_PRI7_R&0xFF00FFFF)|0x00A00000; // (g) priority 2
  NVIC_EN0_R = 0x40000000;      // (h) enable interrupt 30 in NVIC
}
void SWTwoInit(void){
  unsigned long volatile delay;
  SYSCTL_RCGCGPIO_R |= 0x00000020; // (a) activate clock for port F
  delay = SYSCTL_RCGCGPIO_R;
  GPIO_PORTF_LOCK_R = 0x4C4F434B; // unlock GPIO Port F
  GPIO_PORTF_CR_R = 0x01;         // allow changes to PF4,0
  GPIO_PORTF_DIR_R &= ~0x01;    // (c) make PF4,0 in (built-in button)
  GPIO_PORTF_AFSEL_R &= ~0x01;  //     disable alt funct on PF4,0
  GPIO_PORTF_DEN_R |= 0x01;     //     enable digital I/O on PF4,0
  GPIO_PORTF_PCTL_R &= ~0x000F000F; //  configure PF4,0 as GPIO
  GPIO_PORTF_AMSEL_R &= ~0x01;  //     disable analog functionality on PF4,0
  GPIO_PORTF_PUR_R |= 0x01;     //     enable weak pull-up on PF4,0
  GPIO_PORTF_IS_R &= ~0x01;     // (d) PF4,PF0 is edge-sensitive
  GPIO_PORTF_IBE_R &= ~0x01;    //     PF4,PF0 is not both edges
  GPIO_PORTF_IEV_R &= ~0x01;    //     PF4,PF0 falling edge event
  GPIO_PORTF_ICR_R = 0x01;      // (e) clear flags 4,0
  GPIO_PORTF_IM_R |= 0x01;      // (f) arm interrupt on PF4,PF0

  NVIC_PRI7_R = (NVIC_PRI7_R&0xFF00FFFF)|0x00A00000; // (g) priority 5 0x00400000
  NVIC_EN0_R = 0x40000000;      // (h) enable interrupt 30 in NVIC
}

uint32_t OS_SW_Debounce = 100;    
void (*OS_SW1_Task)(void) = NULL; char OS_SW1_PRI = 0;
void (*OS_SW2_Task)(void) = NULL; char OS_SW2_PRI = 0;

void GPIOPortF_Handler(void){
  uint32_t current_time = OS_MsTime();
  if( OS_SW_Debounce < current_time ){
      if( GPIO_PORTF_RIS_R&0x10 && OS_SW1_Task != NULL) { OS_SW1_Task(); }
      if( GPIO_PORTF_RIS_R&0x01 && OS_SW2_Task != NULL) { OS_SW2_Task(); }
 }
	GPIO_PORTF_ICR_R = 0x11;
  OS_SW_Debounce = current_time+250;
}

//******** OS_AddSW1Task *************** 
// add a background task to run whenever the SW1 (PF4) button is pushed
// Inputs: pointer to a void/void background function
//         priority 0 is the highest, 5 is the lowest
// Outputs: 1 if successful, 0 if this thread can not be added
// It is assumed that the user task will run to completion and return
// This task can not spin, block, loop, sleep, or kill
// This task can call OS_Signal  OS_bSignal   OS_AddThread
// This task does not have a Thread ID
// In labs 2 and 3, this command will be called 0 or 1 times
// In lab 2, the priority field can be ignored
// In lab 3, there will be up to four background threads, and this priority field 
//           determines the relative priority of these four threads
int OS_AddSW1Task(void(*task)(void), uint32_t priority){
  OS_SW1_Task = task;
  OS_SW1_PRI = priority;
  priority = OS_SW2_PRI > priority ? OS_SW2_PRI : priority;
  SWOneInit();
  return 1; 
};

//******** OS_AddSW2Task *************** 
// add a background task to run whenever the SW2 (PF0) button is pushed
// Inputs: pointer to a void/void background function
//         priority 0 is highest, 5 is lowest
// Outputs: 1 if successful, 0 if this thread can not be added
// It is assumed user task will run to completion and return
// This task can not spin block loop sleep or kill
// This task can call issue OS_Signal, it can call OS_AddThread
// This task does not have a Thread ID
// In lab 2, this function can be ignored
// In lab 3, this command will be called will be called 0 or 1 times
// In lab 3, there will be up to four background threads, and this priority field 
//           determines the relative priority of these four threads
int OS_AddSW2Task(void(*task)(void), uint32_t priority){
  OS_SW2_Task = task;
  OS_SW2_PRI = priority;
  priority = OS_SW1_PRI > priority ? OS_SW1_PRI : priority;
  SWTwoInit();
  return 1; 
};

void OS_Sleep_List_Push( TCBPtr tcb, uint32_t sleepTime){
    TCBPtr Prev;
    
    sleepTime = sleepTime + OS.SYSTIME_Slice_MS;
    
    if( OS.Sleep_List_Head == NULL ) {
        OS.Sleep_List_Head = tcb;
        OS.Sleep_List_Tail = tcb;
        tcb->sleep_state = sleepTime;   
        return;
    } else if ( sleepTime <= OS.Sleep_List_Head->sleep_state ) {
        tcb->next_tcb = OS.Sleep_List_Head;
        OS.Sleep_List_Head = tcb;
        OS.Sleep_List_Head->sleep_state = sleepTime;
        OS.Sleep_List_Head->next_tcb->sleep_state -= sleepTime;
    } else {
        Prev = OS.Sleep_List_Head;
        sleepTime = sleepTime-OS.Sleep_List_Head->sleep_state;
        while(1){
            if( Prev == OS.Sleep_List_Tail) {
                tcb->sleep_state = sleepTime;
                Prev->next_tcb = tcb;
                OS.Sleep_List_Tail = tcb;
                break;
            } else if ( sleepTime <= Prev->next_tcb->sleep_state  ){
                tcb->sleep_state = sleepTime;
                Prev->next_tcb->sleep_state -= sleepTime;
                tcb->next_tcb = Prev->next_tcb;
                Prev->next_tcb = tcb;
                break;
            } else {
                sleepTime -= Prev->next_tcb->sleep_state;
                Prev = Prev->next_tcb;
            }
        }
    }

    
}

//input:        Elapsed time since last pop event
//side effect:  Update timer 5 to trigger when appropriate;
void OS_Sleep_List_Pop(int32_t elapsed_time_ms){
    long sr;
    sr = OS_SCritical();
    //Check if needed then modifiy linked list    
    while( OS.Sleep_List_Head != NULL && (OS.Sleep_List_Head->sleep_state == 0 || elapsed_time_ms > 0) ){
        if(OS.Sleep_List_Head->sleep_state <= elapsed_time_ms){
            TCBPtr old_sleep_head;
            long sr = OS_SCritical();
            old_sleep_head = OS.Sleep_List_Head;
            
            //If last element, and it wakes, reset state
            if(OS.Sleep_List_Head == OS.Sleep_List_Tail){ OS.Sleep_List_Head = NULL; OS.Sleep_List_Tail = NULL;
            } else {                                      OS.Sleep_List_Head = OS.Sleep_List_Head->next_tcb; }
            
            elapsed_time_ms -= old_sleep_head->sleep_state;
            OS_Return_TCB(old_sleep_head);

        } else {
            OS.Sleep_List_Head->sleep_state = OS.Sleep_List_Head->sleep_state - elapsed_time_ms;
            elapsed_time_ms = 0;
            break;
        }
    }
    OS_ECritical(sr);
}


// ******** OS_Sleep ************
// place this thread into a dormant state
// input:  minimum number of msec to sleep
// output: none
void OS_Sleep(uint32_t sleepTime){  
  Systick_Stop();
  if(sleepTime != 0){ 
    OS.TCB_Jump = OS.TCB_RunPt->next_tcb;
    OS_TCB_Bypass(OS.TCB_RunPt);
    OS_Sleep_List_Push( OS.TCB_RunPt, sleepTime);
  }
  Systick_Continue();
  ContextSwitch();       
};  


// ******** OS_Kill ************
// kill the currently running thread, release its TCB and stack
// input:  none
// output: none
void OS_Kill(void){
  Systick_Stop();
  OS_TCB_Bypass(OS.TCB_RunPt);             // Bypass TCB in linked list
  OS.TCB_Jump = OS.TCB_RunPt->next_tcb;    // Setup TCB Jump
  OS.TCB_RunPt->next_tcb = NULL;           // Deallocate
  OS.TCB_RunPt->sleep_state = -1;  
  OS.TCB_RunPt->blocked_state = -1;
  Systick_Continue();
  ContextSwitch();                          //TODO - Fix: Use OS_Launch to avoid additional pushes.
  for(;;){};                                //Hang on fail
}; 

// ******** OS_Suspend ************
// suspend execution of currently running thread
// scheduler will choose another thread to execute
// Can be used to implement cooperative multitasking 
// Same function as OS_Sleep(0)
// input:  none
// output: none
void OS_Suspend(void){
  Systick_Stop();
  Systick_Restart();
  ContextSwitch();
};
  
// ******** OS_Fifo_Init ************
// Initialize the Fifo to be empty
// Inputs: size
// Outputs: none 
// In Lab 2, you can ignore the size field
// In Lab 3, you should implement the user-defined fifo size
// In Lab 3, you can put whatever restrictions you want on size
//    e.g., 4 to 64 elements
//    e.g., must be a power of 2,4,8,16,32,64,128
void OS_Fifo_Init(uint32_t size){
  // put Lab 2 (and beyond) solution here
  // This code is taken from the textbook chapter 5.4.4
	FifoSize = size;
  PutPt = GetPt = &Fifo[0]; // empty
  OS_InitSemaphore(&CurrentSize, 0);
  OS_InitSemaphore(&RoomLeft, size);
  OS_InitSemaphore(&FIFOMutex, 1);
};

// ******** OS_Fifo_Put ************
// Enter one data sample into the Fifo
// Called from the background, so no waiting 
// Inputs:  data
// Outputs: true if data is properly saved,
//          false if data not saved, because it was full
// Since this is called by interrupt handlers 
//  this function can not disable or enable interrupts
int OS_Fifo_Put(uint32_t data){
  // This code is taken from the textbook chapter 5.4.4
  if(CurrentSize.Value == FifoSize){ return 0; }
  *(PutPt) = data;                // put data in
  PutPt++;												// place for next
  if(PutPt == &Fifo[FifoSize]){
    PutPt = &Fifo[0];             // wrap fifo (if full, put at beginning of queue)
  }
  OS_Signal(&CurrentSize);
  return 1;
};  

// ******** OS_Fifo_Get ************
// Remove one data sample from the Fifo
// Called in foreground, will spin/block if empty
// Inputs:  none
// Outputs: data 
uint32_t OS_Fifo_Get(void){
	uint32_t FifoData;
	OS_Wait(&CurrentSize);
	OS_Wait(&FIFOMutex);
	FifoData = *(GetPt);								// get data
	GetPt++;												// points to next data to get
	if(GetPt == &Fifo[FifoSize]){
		GetPt = &Fifo[0];						// wrap
	}
	OS_Signal(&FIFOMutex);
	return FifoData;
};

// ******** OS_Fifo_Size ************
// Check the status of the Fifo
// Inputs: none
// Outputs: returns the number of elements in the Fifo
//          greater than zero if a call to OS_Fifo_Get will return right away
//          zero or less than zero if the Fifo is empty 
//          zero or less than zero if a call to OS_Fifo_Get will spin or block
int32_t OS_Fifo_Size(void){
  // put Lab 2 (and beyond) solution here
  return CurrentSize.Value; // Since we use semaphore in FIFO get and put, it is easy to access FIFO size
};


// ******** OS_MailBox_Init ************
// Initialize communication channel
// Inputs:  none
// Outputs: none
void OS_MailBox_Init(void){
  // put Lab 2 (and beyond) solution here
  DataValid.Value = 0; // data is initially not valid as there is no 
                // new data that has not been picked up
  BoxFree.Value = 1; // box is initially free (1)
};

// ******** OS_MailBox_Send ************
// enter mail into the MailBox
// Inputs:  data to be sent
// Outputs: none
// This function will be called from a foreground thread
// It will spin/block if the MailBox contains data not yet received 
void OS_MailBox_Send(uint32_t data){
  // put Lab 2 (and beyond) solution here
  OS_bWait(&BoxFree); // need to wait until mailbox is free to send data
  Mailbox = data; // put data into mailbox
  OS_bSignal(&DataValid); // There is valid data in the mailbox
};

// ******** OS_MailBox_Recv ************
// remove mail from the MailBox
// Inputs:  none
// Outputs: data received
// This function will be called from a foreground thread
// It will spin/block if the MailBox is empty 
uint32_t OS_MailBox_Recv(void){
  // put Lab 2 (and beyond) solution here
  uint32_t mailbox_data = 0; // this makes the function thread safe
  OS_bWait(&DataValid); // Wait until there is valid data in the mailbox
  mailbox_data = Mailbox; // receive data from mailbox
  OS_bSignal(&BoxFree); // mailbox is free again
  return mailbox_data; 
};

// ******** OS_TimeDifference ************
// Calculates difference between two times
// Inputs:  two times measured with OS_Time
// Outputs: time difference in 12.5ns units 
// The time resolution should be less than or equal to 1us, and the precision at least 12 bits
// It is ok to change the resolution and precision of this function as long as 
//   this function and OS_Time have the same resolution and precision 
uint32_t OS_TimeDifference(uint32_t start, uint32_t stop){
  uint32_t a = stop-start;
  uint32_t b = 0xFFFFFFFF-stop+start;
  return a < b ? a : b;
};

// ******** OS_ClearMsTime ************
// sets the system time to zero (solve for Lab 1), and start a periodic interrupt
// Inputs:  none
// Outputs: none
// You are free to change how this works
void OS_ClearMsTime(void){  
  SYSCTL_RCGCTIMER_R |= 0x20;   // 0) activate TIMER5
  TIMER5_CTL_R = 0x00000000;    // 1) disable TIMER5A during setup
  TIMER5_CFG_R = 0x00000000;    // 2) configure for 32-bit mode
  TIMER5_TAMR_R = 0x00000012;   // 3) configure for periodic mode, default Up-count settings
  TIMER5_TAILR_R = 800000000 - 1;    // 4) reload value (10 seconds at 80Mhz)
  TIMER5_TAPR_R = 0;            // 5) bus clock resolution
  TIMER5_ICR_R = 0x00000001;    // 6) clear TIMER5A timeout flag
  TIMER5_IMR_R = 0x00000001;    // 7) arm timeout interrupt
  NVIC_PRI23_R = (NVIC_PRI23_R&0xFFFFFF00)|(3<<5); // priority 
  NVIC_EN2_R = 1<<28;           // 9) enable IRQ 92 in NVIC
  TIMER5_CTL_R = 0x00000001;    // 10) enable TIMER5A  
  OS.SYSTIME_MS = 0;
};

void Timer5A_Handler(void){
  TIMER5_ICR_R = TIMER_ICR_TATOCINT;            // Acknowledge TIMER5A timeout
  OS.SYSTIME_MS = OS.SYSTIME_MS + 10000;
}

// ******** OS_Time ************
// return the system time 
// Outputs: time in 12.5ns units, 0 to 4294967295
// The time resolution should be less than or equal to 1us, and the precision 32 bits
// It is ok to change the resolution and precision of this function as long as 
//   this function and OS_TimeDifference have the same resolution and precision 
uint32_t OS_Time(void){
  uint32_t ms,cyc1,cyc2; 
  #ifndef EN_DEBUG_PROFILING
  long sr;
  sr = OS_SCritical();
  #endif
  cyc1 = TIMER5_TAV_R; 
  ms = OS.SYSTIME_MS;
  cyc2 = TIMER5_TAV_R; 
  #ifndef EN_DEBUG_PROFILING
  OS_ECritical(sr);
  #endif
  cyc1 = cyc1<cyc2 ? cyc1 : cyc2; 
  return ms*80000 + cyc1;
};

//Will experience overflow after ~4,295 Seconds (Aprox 72 minutes)
uint32_t OS_UsTime(void){
  return OS_Time()/80;
};

// ******** OS_MsTime ************
// reads the current time in msec (solve for Lab 1)
// Inputs:  none
// Outputs: time in ms units
// You are free to select the time resolution for this function
// For Labs 2 and beyond, it is ok to make the resolution to match the first call to OS_AddPeriodicThread
uint32_t OS_MsTime(void){
  uint32_t ms,cyc1,cyc2; long sr;
  sr = OS_SCritical();
  cyc1 = TIMER5_TAV_R; 
  ms = OS.SYSTIME_MS;
  cyc2 = TIMER5_TAV_R; 
  OS_ECritical(sr);
  cyc1 = cyc1<cyc2 ? cyc1 : cyc2; 
  return ms + (cyc1/80000);  
};


//******** OS_Launch *************** 
// start the scheduler, enable interrupts
// Inputs: number of 12.5ns clock cycles for each time slice
//         you may select the units of this parameter
// Outputs: none (does not return)
// In Lab 2, you can ignore the theTimeSlice field
// In Lab 3, you should implement the user-defined TimeSlice field
// It is ok to limit the range of theTimeSlice to match the 24-bit SysTick
void OS_Launch(uint32_t theTimeSlice){
  OS.TCB_RunPt = OS.TCB_Head;
  OS.SYSTIME_Slice_MS = (theTimeSlice+79999)/80000;
  #ifdef EN_DEBUG_PROFILING
	OS.inCriticalMax=0;
    OS.ltime = OS_Time();
  #endif
  SysTick_Init( theTimeSlice );
  EnableInterrupts();
  StartOS();
};

//************** I/O Redirection *************** 
// redirect terminal I/O to UART or file (Lab 4)

int StreamToDevice=0;                // 0=UART, 1=stream to file (Lab 4)

int fputc (int ch, FILE *f) { 
  if(StreamToDevice==1){  // Lab 4
    if(eFile_Write(ch)){          // close file on error
       OS_EndRedirectToFile(); // cannot write to file
       return 1;                  // failure
    }
    return 0; // success writing
  }
  
  // default UART output
  UART_OutChar(ch);
  return ch; 
}

int fgetc (FILE *f){
  char ch = UART_InChar();  // receive from keyboard
  UART_OutChar(ch);         // echo
  return ch;
}

int OS_RedirectToFile(const char *name){  // Lab 4
  eFile_Create(name);              // ignore error if file already exists
  if(eFile_WOpen(name)) return 1;  // cannot open file
  StreamToDevice = 1;
  return 0;
}

int OS_EndRedirectToFile(void){  // Lab 4
  StreamToDevice = 0;
  if(eFile_WClose()) return 1;    // cannot close file
  return 0;
}

int OS_RedirectToUART(void){
  StreamToDevice = 0;
  return 0;
}

int OS_RedirectToST7735(void){
  return 1;
}

