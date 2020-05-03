/* Copyright (C) 2017 Daniel Page <csdsp@bristol.ac.uk>
 *
 * Use of this source code is restricted per the CC BY-NC-ND license, a copy of 
 * which can be found via http://creativecommons.org (and should be included as 
 * LICENSE.txt within the associated archive or repository).
 */

#include "hilevel.h"
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
    memcpy( ctx, &next->ctx, sizeof( ctx_t ) );                       // Restore execution context of P_{next}
    next_pid = '0' + next->pid;
  }

  PL011_putc( UART0, '[',      true );
  PL011_putc( UART0, prev_pid, true );  
  PL011_putc( UART0, '-',      true );
  PL011_putc( UART0, '>',      true );
  PL011_putc( UART0, next_pid, true );
  PL011_putc( UART0, ']',      true );

  executing = next;                                                   // Update executing process to P_{next}

  return;
}

void schedule( ctx_t* ctx ) {
  int max_priority = -1;
  int max_index;
  for( int i = 0; i < MAX_PROCS; i ++ ) {
    if( executing->pid == procTab[ i ].pid ) {
      continue;
    } else {
      procTab[ i ].age ++;
    }
  }

  for( int i = 0; i < MAX_PROCS; i ++ ) {                               // 找出最大的优先级的值与其序列数
    if( procTab[ i ].priority + procTab[ i ].age > max_priority) {
      max_priority = procTab[ i ].priority + procTab[ i ].age;
      max_index = i;
    }
  }

  procTab[ max_index ].age = 0;
  dispatch( ctx, executing, &procTab[ max_index ]);

  return;
}

pcb_t* get_next_PCB() {
  for( int i = 0; i < MAX_PROCS; i++ ){
    if( procTab[ i ].status == STATUS_CREATED ){
      return &procTab[ i ];
      break;
    }
    return NULL;
  }
}
extern void     main_console(); 
extern uint32_t tos_console;
extern uint32_t tos_user;

void hilevel_handler_rst( ctx_t* ctx ) {

  for( int i = 0; i < MAX_PROCS; i++ ) {
    procTab[ i ].status = STATUS_INVALID;                             // Initialised STATUS_INVALID;
  }

  /* Automatically execute the user programs P3, P4 and P5 by setting the fields
   * in two associated PCBs.  Note in each case that
   *    
   * - the CPSR value of 0x50 means the processor is switched into USR mode, 
   *   with IRQ interrupts enabled, and
   * - the PC and SP values match the entry point and top of stack. 
   */

  memset( &procTab[ 0 ], 0, sizeof( pcb_t ) );                        // Initialise PCB console
  procTab[ 0 ].pid      = 0;                                          // Set pid = 0
  procTab[ 0 ].status   = STATUS_READY;
  procTab[ 0 ].tos      = ( uint32_t )( &tos_console );
  procTab[ 0 ].ctx.cpsr = 0x50;
  procTab[ 0 ].ctx.pc   = ( uint32_t )( &main_console );
  procTab[ 0 ].ctx.sp   = procTab[ 0 ].tos;
  procTab[ 0 ].age      = 0;
  procTab[ 0 ].priority = 2;

  for( int i = 1; i < MAX_PROCS; i++ ){
    memset( &procTab[ 1 ], 0, sizeof( pcb_t ) );                      // Initialise dynamic PCBs
    procTab[ i ].pid      = i;                                        // Set pid = i
    procTab[ i ].status   = STATUS_CREATED;
    procTab[ i ].tos      = ( uint32_t )( &tos_console ) - ( i * OFFSET );
    procTab[ i ].ctx.cpsr = 0x50;
    procTab[ i ].ctx.sp   = procTab[ i ].tos;
    procTab[ i ].age      = 0;
    procTab[ i ].priority = 1;
  }

  TIMER0->Timer1Load  = 0x00100000;                                   // Select period = 2^20 ticks ~= 1 sec
  TIMER0->Timer1Ctrl  = 0x00000002;                                   // Select 32-bit   timer
  TIMER0->Timer1Ctrl |= 0x00000040;                                   // Select periodic timer
  TIMER0->Timer1Ctrl |= 0x00000020;                                   // Enable          timer interrupt
  TIMER0->Timer1Ctrl |= 0x00000080;                                   // Enable          timer

  GICC0->PMR          = 0x000000F0;                                   // Unmask all      interrupts
  GICD0->ISENABLER1  |= 0x00000010;                                   // Enable timer    interrupt
  GICC0->CTLR         = 0x00000001;                                   // Enable GIC      interface
  GICD0->CTLR         = 0x00000001;                                   // Enable GIC      distributor
  
  dispatch( ctx, NULL, &procTab[ 0 ] );
  int_enable_irq();

  return;
}

void hilevel_handler_irq( ctx_t* ctx ) {
  // Step 2: read  the interrupt identifier so we know the source.
  uint32_t id = GICC0->IAR;

  // Step 4: handle the interrupt, then clear (or reset) the source.
  if( id == GIC_SOURCE_TIMER0 ) {
    PL011_putc( UART0, '[', true );
    PL011_putc( UART0, 'T', true );
    PL011_putc( UART0, 'I', true );
    PL011_putc( UART0, 'M', true );
    PL011_putc( UART0, 'E', true );
    PL011_putc( UART0, 'R', true );
    PL011_putc( UART0, ']', true );
    schedule( ctx );
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

    case 0x01 : { // 0x01 =>   write( fd, x, n )
      int   fd = ( int   )( ctx->gpr[ 0 ] );  
      char*  x = ( char* )( ctx->gpr[ 1 ] );  
      int    n = ( int   )( ctx->gpr[ 2 ] ); 

      for( int i = 0; i < n; i++ ) {
        PL011_putc( UART0, *x++, true );
      }
      
      ctx->gpr[ 0 ] = n;

      break;
    }

    case 0x03 : { // 0x03 => SYS_FORK()
      procTab[ 0 ].priority += 1;

      pcb_t* child = get_next_PCB();
      if ( child != NULL ) {
        memcpy( &child->ctx, ctx, sizeof( ctx_t ) );
        child->status = STATUS_READY;
      }

      uint32_t sp_offset = ( uint32_t ) &executing->tos - ctx->sp;
      child->ctx.sp = child->tos - sp_offset;
      memcpy( ( void* ) child->ctx.sp, ( void* ) ctx->sp, sp_offset);

      child->ctx.gpr[ 0 ] = 0;
      ctx->gpr[ 0 ] = child->pid;

      break;
    }

    case 0x04 : { // 0x04 => SYS_EXIT()
      break;
    }

    case 0x05 : {
      ctx->pc = ctx->gpr[0];

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
