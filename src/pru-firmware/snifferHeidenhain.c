/*
 * Heidenhain Encoder Sniffer
 *
 * For Delta Undulator Feed-forward corrections
 * 1-channel sniffer
 *
 * To be run on a BeagleBone-AI PRU.
 * THIS FIRST VERSION IS TO BE RUN ON PRU 2_0 (THIRD PRU = REMOTEPROC6)
 *
 *
 * Under development
 * Patricia Nallin - CNPEM
 * patricia.nallin@cnpem.br
 *
 * */

#include <stdint.h>
#include <stdio.h>
#include <pru_cfg.h>
#include <pru_ctrl.h>
#include <pru_intc.h>
#include "resource_table_empty.h"


// -----------------------------------------------
// For compiling for a different PRU
// EDIT HERE ONLY
// -----------------------------------------------
//#define subsystemPRU            1   // 1 or 2
//#define devPRU                  1   // 0 or 1

#define CLK_ENDAT               ((__R31 >> tbit_c) & 0x01)
#define DATA_ENDAT              ((__R31 >> tbit_d) & 0x01)
// -----------------------------------------------


volatile register uint32_t __R31;
volatile register uint32_t __R30;

#define PRU_SHARED_MEM_ADDR     0x00010000

#define RETURN_LINE_TIMEOUT     -1
#define RETURN_OK               0

#define FALLING_EDGE            0
#define RISING_EDGE             1


volatile uint8_t continue_allowed;
volatile uint8_t tbit_c, tbit_d;


void dummyCycles(uint8_t nCycles, uint16_t samplingEdge);
uint32_t readBits(uint8_t nBits, uint16_t samplingEdge);



void main(){
   // General variables
   uint8_t loop=0;
   uint8_t offsetCommand, offsetPosM, offsetPosL, offsetCRC, offsetMutex;
   uint8_t subsystemPRU, devPRU, unit;

   // Clear SYSCFG[STANDBY_INIT] to enable OCP master port->Shared memory
   CT_CFG.SYSCFG_bit.STANDBY_INIT = 0;

   // Pointer to shram
   volatile int* shram = (volatile int *) PRU_SHARED_MEM_ADDR;

   /* GPI Mode 0, GPO Mode 0 */
   CT_CFG.GPCFG0 = 0;


   devPRU = shram[0] & 0x01;                // Should be 0 or 1
   subsystemPRU = (shram[0] >> 1) & 0xFF;   // Should be 1 or 2

   tbit_c = (shram[0] >> 8) & 0xFF;
   tbit_d = (shram[0] >> 16) & 0xFF;

   shram[0] = 0;

   offsetCommand = (devPRU * 8) + 1;
   offsetPosL = (devPRU * 8) + 2;
   offsetPosM = (devPRU * 8) + 3;
   offsetCRC = (devPRU * 8) + 4;
   offsetMutex = (devPRU * 8) + 5;

   unit = ((subsystemPRU - 1)*2) + devPRU;

   switch(unit)
   {
   case 0:
       tbit_c = 17;
       tbit_d =  5;
       break;
   case 1:
       tbit_c = 18;
       tbit_d =  9;
       break;
   case 2:
       tbit_c =  4;
       tbit_d =  3;
       break;
   case 3:
       tbit_c = 10;
       tbit_d = 11;
       break;
   }

   shram[20] = tbit_c + (tbit_d << 8) + (unit << 16);




   while(1){

       // Start on a beginning of communication. Wait clock HIGH for, at least, 30 us
       for(loop=1; loop!=100; loop++)
       {
           if(((__R31 >> tbit_c) & 0x01) == 1)
           {
               // Reset loop counting
               loop = 1;
           }
       }
       continue_allowed = 1;

       // ----------------------------------------
       // Wait START OF TRANSMISSION - CLK |_
       while(((__R31 >> tbit_c) & 0x01) == 1)
       {
       }

       // ----------------------------------------
       // Wait two clock cycles
       dummyCycles(2, RISING_EDGE);

       // ----------------------------------------
       // Get Mode Command
       if(continue_allowed)
       {
           shram[offsetCommand] = readBits(8, RISING_EDGE);
       }

       // ----------------------------------------
       // Wait START OF REPLY - DATA _|
       for(loop=1; loop!=100; loop++)
       {
           if(((__R31 >> tbit_d) & 0x01) == 1)
           {
               break;
           }
           continue_allowed = 0;
       }

       // Wait clock LOW
       for(loop=1; loop!=100; loop++)
       {
           if(((__R31 >> tbit_c) & 0x01) == 0)
           {
               break;
           }
           continue_allowed = 0;
       }
       continue_allowed = 1;

       // Skip two clock cycles - Error Flags
       dummyCycles(2, RISING_EDGE);


       // ----------------------------------------
       // Get Data (position value) + CRC
       if(continue_allowed)
       {
           // Set mutex
           shram[offsetMutex] = 0;
           // Acquire POSITION
           shram[offsetPosL] = readBits(32, FALLING_EDGE);
           shram[offsetPosM] = readBits(3, FALLING_EDGE);
           // Acquire CRC
           shram[offsetCRC] = readBits(5, FALLING_EDGE);
           // Clear mutex
           shram[offsetMutex] = 1;
       }
       //__delay_cycles(100000000); // half-second delay
   }
}


void dummyCycles(uint8_t nCycles, uint16_t samplingEdge)
{
    uint16_t wait;
    for(; nCycles != 0; nCycles--)
           {
               // Wait RISING EDGE
               wait = 0;
               while(((__R31 >> tbit_c) & 0x01) != samplingEdge)
               {
                   wait++;
                   if(wait == 0)
                   {
                       continue_allowed = 0;
                       return;
                   }
               }
               // Wait FALLING EDGE
               wait = 0;
               while(((__R31 >> tbit_c) & 0x01) == 1)
               {
                   wait++;
                   if(wait == 0)
                   {
                       continue_allowed = 0;
                       return;
                   }
               }
           }
    return;
}

uint32_t readBits(uint8_t nBits, uint16_t samplingEdge)
{
    uint16_t wait;
    uint32_t data = 0;
    for(; nBits != 0; nBits--)
           {
               // Wait SAMPLING EDGE
               wait = 0;
               while(((__R31 >> tbit_c) & 0x01) != samplingEdge)
               {
                   wait++;
                   if(wait == 0)
                   {
                       continue_allowed = 0;
                       return 0;
                   }
               }

               // GET DATA INFO

               data = (data << 1) + ((__R31 >> tbit_d) & 0x01);

               // Wait complementary
               wait = 0;
               while(((__R31 >> tbit_c) & 0x01) == samplingEdge)
               {
                   wait++;
                   if(wait == 0)
                   {
                       continue_allowed = 0;
                       return 0;
                   }
               }
           }
    return data;
}