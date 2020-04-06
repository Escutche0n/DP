/* Copyright (C) 2017 Daniel Page <csdsp@bristol.ac.uk>
 *
 * Use of this source code is restricted per the CC BY-NC-ND license, a copy of 
 * which can be found via http://creativecommons.org (and should be included as 
 * LICENSE.txt within the associated archive or repository).
 */

#include "hilevel.h"
#define OFFSET 0x00001000                                                   // Define the space offset for each stack

pcb_t procTab[ MAX_PROCS ]; pcb_t* executing = NULL;

// Copied Dispatch Function from lab 3
void dispatch( ctx_t* ctx, pcb_t* prev, pcb_t* next ) {
  char prev_pid = '?', next_pid = '?';

  if( NULL != prev ) {
    memcpy( &prev->ctx, ctx, sizeof( ctx_t ) );                     // Preserve execution context of P_{prev}
    prev_pid = '0' + prev->pid;
  }
  if( NULL != next ) {
    memcpy( ctx, &next->ctx, sizeof( ctx_t ) );                     // Restore  execution context of P_{next}
    next_pid = '0' + next->pid;
  }

    PL011_putc( UART0, '[',      true );
    PL011_putc( UART0, prev_pid, true );
    PL011_putc( UART0, '-',      true );
    PL011_putc( UART0, '>',      true );
    PL011_putc( UART0, next_pid, true );
    PL011_putc( UART0, ']',      true );

    executing = next;                                                                      // Update   executing process to P_{next}

  return;
}

// Copied Schedule Function from lab 3
void schedule( ctx_t* ctx ) {

  if     ( executing->pid == procTab[ 0 ].pid ) {
    dispatch( ctx, &procTab[ 0 ], &procTab[ 1 ] );                // context switch P_3 -> P_4

    procTab[ 0 ].status = STATUS_READY;                             // update   execution status  of P_3 
    procTab[ 1 ].status = STATUS_EXECUTING;                  // update   execution status  of P_4
  }
  else if( executing->pid == procTab[ 1 ].pid ) {
    dispatch( ctx, &procTab[ 1 ], &procTab[ 2 ] );               // context switch P_4 -> P_5

    procTab[ 1 ].status = STATUS_READY;                            // update   execution status  of P_4
    procTab[ 2 ].status = STATUS_EXECUTING;                 // update   execution status  of P_5
  }
  else if( executing->pid == procTab[ 2 ].pid ) {
  dispatch( ctx, &procTab[ 2 ], &procTab[ 0 ] );               // context switch P_5 -> P_3

  procTab[ 2 ].status = STATUS_READY;                            // update   execution status  of P_5
  procTab[ 0 ].status = STATUS_EXECUTING;                 // update   execution status  of P_3
}

  return;
}

extern void     main_P3(); 
extern uint32_t tos_P3;
extern void     main_P4(); 
extern uint32_t tos_P4;
extern void     main_P5(); 
extern uint32_t tos_P5;

void hilevel_handler_rst(ctx_t* ctx              ) {
  /* Invalidate all entries in the process table, so it's clear they are not
   * representing valid (i.e., active) processes.
   */

  for( int i = 0; i < MAX_PROCS; i++ ) {
    procTab[ i ].status = STATUS_INVALID;
  }

  /* Automatically execute the user programs P1 and P2 by setting the fields
   * in two associated PCBs.  Note in each case that
   *    
   * - the CPSR value of 0x50 means the processor is switched into USR mode, 
   *   with IRQ interrupts enabled, and
   * - the PC and SP values match the entry point and top of stack. 
   */

  memset( &procTab[ 0 ], 0, sizeof( pcb_t ) ); // initialise 0-th PCB = P_1
  procTab[ 0 ].pid      = 1;
  procTab[ 0 ].status   = STATUS_READY;
  procTab[ 0 ].tos      = ( uint32_t )( &tos_P3  );
  procTab[ 0 ].ctx.cpsr = 0x50;
  procTab[ 0 ].ctx.pc   = ( uint32_t )( &main_P3 );
  procTab[ 0 ].ctx.sp   = procTab[ 0 ].tos;

  memset( &procTab[ 1 ], 0, sizeof( pcb_t ) ); // initialise 1-st PCB = P_2
  procTab[ 1 ].pid      = 2;
  procTab[ 1 ].status   = STATUS_READY;
  procTab[ 1 ].tos      = ( uint32_t )( &tos_P4  );
  procTab[ 1 ].ctx.cpsr = 0x50;
  procTab[ 1 ].ctx.pc   = ( uint32_t )( &main_P4 );
  procTab[ 1 ].ctx.sp   = procTab[ 1 ].tos;

  memset( &procTab[ 2 ], 0, sizeof( pcb_t ) ); // initialise 2-nd PCB = P_3
  procTab[ 2 ].pid      = 3;
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

  return;
}

void hilevel_handler_irq() {
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

    default   : { // 0x?? => unknown/unsupported
      break;
    }
  }

  return;
}