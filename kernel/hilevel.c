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
  char prev_pid = '?', nex_pid = '?';

  if( NULL != prev ) {
    memcpy( &prev->ctx, ctx, sizeof( ctx_t ) );                       // Preserve execution context of P_{prev}
    prev_pid = '0' + prev->pid;
  }
  if( NULL != next ) {
    memcpy( ctx, &next->ctx, sizeof( ctx_t ) );                       // Restore execution context of P_{next}
    nex_pid = '0' + next->pid;
  }

  PL011_putc( UART0, '[',      true );
  PL011_putc( UART0, prev_pid, true );  
  PL011_putc( UART0, '-',      true );
  PL011_putc( UART0, '>',      true );
  PL011_putc( UART0, nex_pid, true );
  PL011_putc( UART0, ']',      true );

  executing = next;                                                   // Update executing process to P_{next}

  return;
}

void schedule( ctx_t* ctx ) {
  int max_pri = -1;
  int nex_pid;
  
  for( int i = 0; i < MAX_PROCS; i ++ ) {
    if( procTab[ i ].priority + procTab[ i ].age > max_pri ) {   // Update max_pri and nex_pid
      max_pri = procTab[ i ].priority + procTab[ i ].age;
      nex_pid = i;
    }
  }

  for( int i = 0; i < MAX_PROCS; i ++ ) {                             // age++
    if( nex_pid == i || procTab[ i ].status == STATUS_CREATED || procTab[ i ].status == STATUS_TERMINATED ) {
      continue;                                                       // Ageing if not creating, terminating, coming pids
    }
    procTab[ i ].age += 1;
  }
  procTab[ nex_pid ].age = 0;                                        

  if( procTab[ nex_pid ].status == STATUS_READY && executing->status != STATUS_TERMINATED ){
    executing->status = STATUS_READY;
    procTab[ nex_pid ].status = STATUS_EXECUTING;
  }
  if( executing->status == STATUS_TERMINATED ){
    procTab[ nex_pid ].status = STATUS_EXECUTING;
  }
  dispatch( ctx, executing, &procTab[ nex_pid ]);
}

pcb_t* get_next_PCB() {
  pcb_t* next_empty = NULL;
  for ( int i = 0; i < MAX_PROCS; i++ ){
    if ( procTab[ i ].status == STATUS_CREATED){
     next_empty = &procTab[ i ];
    break;
    }
  }
  return next_empty;
}

extern void     main_console(); 
extern uint32_t tos_console;

void hilevel_handler_rst( ctx_t* ctx ) {

  for( int i = 0; i < MAX_PROCS; i++ ) {
    procTab[ i ].status = STATUS_INVALID;                             // Initialised STATUS_INVALID;
  }

  memset( &procTab[ 0 ], 0, sizeof( pcb_t ) );                        // Initialise PCB console
  procTab[ 0 ].pid      = 0;                                          // Set pid = 0
  procTab[ 0 ].status   = STATUS_READY;
  procTab[ 0 ].tos      = ( uint32_t )( &tos_console );
  procTab[ 0 ].ctx.cpsr = 0x50;
  procTab[ 0 ].ctx.pc   = ( uint32_t )( &main_console );
  procTab[ 0 ].ctx.sp   = procTab[ 0 ].tos;
  procTab[ 0 ].priority = 2;
  procTab[ 0 ].age      = 0;

  for( int i = 1; i < MAX_PROCS; i++ ){
    memset( &procTab[ i ], 0, sizeof( pcb_t ) );                      // Initialise dynamic PCBs
    procTab[ i ].pid      = i;                                        // Set pid = i
    procTab[ i ].status   = STATUS_CREATED;
    procTab[ i ].tos      = ( uint32_t )( &tos_console ) - ( i * OFFSET );
    procTab[ i ].ctx.cpsr = 0x50;
    procTab[ i ].ctx.sp   = procTab[ i ].tos;
    procTab[ i ].priority = 1;
    procTab[ i ].age      = 0;
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

    case 0x03 : { // 0x03 => SYS_FORK()
      procTab[ 0 ].priority += 1;

      pcb_t* child = get_next_PCB();
      if ( child != NULL ) {
        memcpy( &child->ctx, ctx, sizeof( ctx_t ) );
        child->status = STATUS_READY;

        uint32_t sp_offset = ( uint32_t ) &executing->tos - ctx->sp;
        child->ctx.sp = child->tos - sp_offset;
        memcpy( ( void* ) child->ctx.sp, ( void* ) ctx->sp, sp_offset);

        child->ctx.gpr[ 0 ] = 0;
        ctx->gpr[ 0 ] = child->pid;

        break;
      } else {
        ctx->gpr[ 0 ] = executing->pid;
        break;
      }
    }

    case 0x04 : { // 0x04 => SYS_EXIT()
      PL011_putc( UART0, '[', true );
      PL011_putc( UART0, 'E', true );
      PL011_putc( UART0, 'X', true );
      PL011_putc( UART0, 'I', true );
      PL011_putc( UART0, 'T', true );
      PL011_putc( UART0, ']', true );

      executing->status = STATUS_TERMINATED;
      executing->age    = 0;
      schedule( ctx );

      break;
    }

    case 0x05 : { // 0x05 => SYS_EXEC()
      // PL011_putc( UART0, '[', true );
      // PL011_putc( UART0, 'E', true );
      // PL011_putc( UART0, 'X', true );
      // PL011_putc( UART0, 'E', true );
      // PL011_putc( UART0, 'C', true );
      // PL011_putc( UART0, ']', true );
      ctx->pc = ctx->gpr[0];
      break;
    }

    case 0x06 : { // 0x06 => SYS_KILL()
      int pid = ctx->gpr[0];
      if( pid == -1 ){
        for( int i = 1; i < 20; i++ ){
          procTab[i].status = STATUS_TERMINATED;
        }
        procTab[0].priority = 2;
      }
      else if(pid >= 0 && pid < MAX_PROCS){
          procTab[pid].status = STATUS_TERMINATED;
          procTab[0].priority -= 1;
      }
      schedule(ctx);
      break;
    }

    default   : { // 0x?? => unknown/unsupported
      break;
    }
  }

  return;
}