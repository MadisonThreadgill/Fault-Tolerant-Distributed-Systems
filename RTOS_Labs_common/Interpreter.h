/**
 * @file      Interpreter.h
 * @brief     Real Time Operating System for Labs 2, 3 and 4
 * @details   EE445M/EE380L.6
 * @version   V1.0
 * @author    Valvano
 * @copyright Copyright 2020 by Jonathan W. Valvano, valvano@mail.utexas.edu,
 * @warning   AS-IS
 * @note      For more information see  http://users.ece.utexas.edu/~valvano/
 * @date      Jan 5, 2020

 ******************************************************************************/





/**
 * @details  Run the user interface.
 * @param  none
 * @return none
 * @brief  Interpreter
 */
 
#include <stdint.h>

// MT defined these here to use in the interpreter command
extern int32_t MaxJitter;             // largest time jitter between interrupts in usec
extern uint32_t NumCreated;           // number of foreground threads created
extern uint32_t NumSamples;   				// incremented every ADC sample, in Producer
extern uint32_t PIDWork;              // current number of PID calculations finished
extern uint32_t FilterWork;           // number of digital filter calculations finished
extern int32_t x[64],y[64];           // input and output arrays for FFT
extern uint32_t DataLost;             // data sent by Producer, but not received by Consumer
extern uint32_t JitterHistogram[];
extern uint32_t const JitterSize;
extern uint32_t CPUUtil;       							// calculated CPU utilization (in 0.01%)

 
void Interpreter(void);
void Interpreter_Remote(void);

