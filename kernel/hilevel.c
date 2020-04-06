/* Copyright (C) 2017 Daniel Page <csdsp@bristol.ac.uk>
 *
 * Use of this source code is restricted per the CC BY-NC-ND license, a copy of 
 * which can be found via http://creativecommons.org (and should be included as 
 * LICENSE.txt within the associated archive or repository).
 */

#include "hilevel.h"
#include <string.h>
#define OFFSET 0x00001000                                             // Define the space offset for each stack

pcb_t procTab[ MAX_PROCS ]; pcb_t* executing = NULL;

// Copied Dispatch Function from lab 3
void dispatch( ctx_t* ctx, pcb_t* prev, pcb_t* next ) {
  char prev_pid = '?', next_pid = '?';

  if( NULL != prev ) {
    memcpy( &prev->ctx, ctx, sizeof( ctx_t ) );                       // Preserve execution context of P_{prev}
    prev_pid = '0' + prev->pid;
  }
  if( NULL != next ) {
    memcpy( ctx, &next->ctx, sizeof( ctx_t ) );                       // Restore  execution context of P_{next}
    next_pid = '0' + next->pid;
  }

    PL011_putc( UART0, '[',      true );
    PL011_putc( UART0, prev_pid, true );
    PL011_putc( UART0, '-',      true );
    PL011_putc( UART0, '>',      true );
    PL011_putc( UART0, next_pid, true );
    PL011_putc( UART0, ']',      true );

    executing = next;                                                 // Update   executing process to P_{next}

  return;
}

// Copied Schedule Function from lab 3
void schedule( ctx_t* ctx ) {
  if     ( executing->pid == procTab[ 0 ].pid ) {
    dispatch( ctx, &procTab[ 0 ], &procTab[ 1 ] );                    // Context switch P_3 -> P_4

    procTab[ 0 ].status = STATUS_READY;                               // Update   execution status  of P_3 
    procTab[ 1 ].status = STATUS_EXECUTING;                           // Update   execution status  of P_4
  }
  else if( executing->pid == procTab[ 1 ].pid ) {
    dispatch( ctx, &procTab[ 1 ], &procTab[ 2 ] );                    // Context switch P_4 -> P_5

    procTab[ 1 ].status = STATUS_READY;                               // Update   execution status  of P_4
    procTab[ 2 ].status = STATUS_EXECUTING;                           // Update   execution status  of P_5
  }
  else if( executing->pid == procTab[ 2 ].pid ) {
    dispatch( ctx, &procTab[ 2 ], &procTab[ 0 ] );                    // Context switch P_5 -> P_3

    procTab[ 2 ].status = STATUS_READY;                               // Update   execution status  of P_5
    procTab[ 0 ].status = STATUS_EXECUTING;                           // Update   execution status  of P_3
  }
  return;
}

extern void     main_P3(); 
extern uint32_t tos_P3;
extern void     main_P4(); 
extern uint32_t tos_P4;
extern void     main_P5(); 
extern uint32_t tos_P5;

void hilevel_handler_rst( ctx_t* ctx ) {
  /* Invalidate all entries in the process table, so it's clear they are not
   * representing valid (i.e., active) processes.
   */
  TIMER0->Timer1Load  = 0x00100000;                                   // Select period = 2^20 ticks ~= 1 sec
  TIMER0->Timer1Ctrl  = 0x00000002;                                   // Select 32-bit   timer
  TIMER0->Timer1Ctrl |= 0x00000040;                                   // Select periodic timer
  TIMER0->Timer1Ctrl |= 0x00000020;                                   // Enable          timer interrupt
  TIMER0->Timer1Ctrl |= 0x00000080;                                   // Enable          timer

  GICC0->PMR          = 0x000000F0;                                   // Unmask all      interrupts
  GICD0->ISENABLER1  |= 0x00000010;                                   // Enable timer    interrupt
  GICC0->CTLR         = 0x00000001;                                   // Enable GIC      interface
  GICD0->CTLR         = 0x00000001;                                   // Enable GIC      distributor

  for( int i = 0; i < MAX_PROCS; i++ ) {
    procTab[ i ].status = STATUS_INVALID;
  }

  /* Automatically execute the user programs P3, P4 and P5 by setting the fields
   * in two associated PCBs.  Note in each case that
   *    
   * - the CPSR value of 0x50 means the processor is switched into USR mode, 
   *   with IRQ interrupts enabled, and
   * - the PC and SP values match the entry point and top of stack. 
   */

  memset( &procTab[ 0 ], 0, sizeof( pcb_t ) );                        // Initialise 0-th PCB = P_3
  procTab[ 0 ].pid      = 1;                                          // Set pid = 1
  procTab[ 0 ].status   = STATUS_READY;
  procTab[ 0 ].tos      = ( uint32_t )( &tos_P3  );
  procTab[ 0 ].ctx.cpsr = 0x50;
  procTab[ 0 ].ctx.pc   = ( uint32_t )( &main_P3 );
  procTab[ 0 ].ctx.sp   = procTab[ 0 ].tos;

  memset( &procTab[ 1 ], 0, sizeof( pcb_t ) );                        // Initialise 1-st PCB = P_4
  procTab[ 1 ].pid      = 2;                                          // Set pid = 2
  procTab[ 1 ].status   = STATUS_READY;
  procTab[ 1 ].tos      = ( uint32_t )( &tos_P4  );
  procTab[ 1 ].ctx.cpsr = 0x50;
  procTab[ 1 ].ctx.pc   = ( uint32_t )( &main_P4 );
  procTab[ 1 ].ctx.sp   = procTab[ 1 ].tos;

  memset( &procTab[ 2 ], 0, sizeof( pcb_t ) );                        // Initialise 2-nd PCB = P_5
  procTab[ 2 ].pid      = 3;                                          // Set pid = 3
  procTab[ 2 ].status   = STATUS_READY;
  procTab[ 2 ].tos      = ( uint32_t )( &tos_P5  );
  procTab[ 2 ].ctx.cpsr = 0x50;
  procTab[ 2 ].ctx.pc   = ( uint32_t )( &main_P5 );
  procTab[ 2 ].ctx.sp   = procTab[ 2 ].tos;

  /* Once the PCBs are initialised, we arbitrarily select the 0-th PCB to be 
   * executed: there is no need to preserve the execution context, since it 
   * is invalid on reset (i.e., no process was previously executing).
   */

  dispatch( ctx, NULL, &procTab[ 0 ] );
  int_enable_irq();

  return;
}

void hilevel_handler_irq( ctx_t* ctx ) {
  // Step 2: read  the interrupt identifier so we know the source.
  uint32_t id = GICC0->IAR;

  // Step 4: handle the interrupt, then clear (or reset) the source.
  if( id == GIC_SOURCE_TIMER0 ) {
    schedule( ctx );
    PL011_putc( UART0, 'T', true );
    TIMER0->Timer1IntClr = 0x01;
  }

  // Step 5: write the interrupt identifier to signal we're done.
  GICC0->EOIR = id;

  return;
}

void hilevel_handler_svc( ctx_t* ctx, uint32_t id ) {
  switch( id ) {
    case 0x00 : { // 0x00 => yield()
      schedule( ctx );
      break;
    }

    case 0x01 : { // 0x01 => write( fd, x, n )
      int   fd = ( int   )( ctx->gpr[ 0 ] );  
      char*  x = ( char* )( ctx->gpr[ 1 ] );  
      int    n = ( int   )( ctx->gpr[ 2 ] ); 

      for( int i = 0; i < n; i++ ) {
        PL011_putc( UART0, *x++, true );
      }
      
      ctx->gpr[ 0 ] = n;

      break;
    }

    case 0x03 : {
      break;
    }

    case 0x04 : {
      break;
    }

    case 0x05 : {
      break;
    }

    case 0x06 : {
      break;
    }

    case 0x07 : {
      break;
    }

    default   : { // 0x?? => unknown/unsupported
      break;
    }
  }

  return;
}