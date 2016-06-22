#include "schedos-app.h"
#include "x86sync.h"

/*****************************************************************************
 * schedos-1
 *
 *   This tiny application prints red "1"s to the console.
 *   It yields the CPU to the kernel after each "1" using the sys_yield()
 *   system call.  This lets the kernel (schedos-kern.c) pick another
 *   application to run, if it wants.
 *
 *   The other schedos-* processes simply #include this file after defining
 *   PRINTCHAR appropriately.
 *
 *****************************************************************************/

#ifndef PRINTCHAR
#define PRINTCHAR	('1' | 0x0C00)
#endif

// Exercise 4A
// Set the value below to the desired priority level number
#ifndef T_PRIORITY
#define T_PRIORITY 1
#endif

// Exercise 4B
// Set the value below to the desired share number
#ifndef T_SHARE
#define T_SHARE 1
#endif

// Exercise 6, 8
// Set the value below to 1 to enable atomic write
#ifndef ATOMIC_WRITE
#define ATOMIC_WRITE 0
#endif

// Exercise 8:
// Set the value below to 1 to enable
// alternative synchronization mechanism using
// compare_and_swap
#ifndef COMPARE_N_SWAP
#define COMPARE_N_SWAP 0
#endif



void
start(void)
{
    // Tests for Exercise 4A / 4B
    sys_set_priority(T_PRIORITY);   // 4A
    sys_set_share(T_SHARE);         // 4B
    
	int i;

	for (i = 0; i < RUNCOUNT; i++) {
		// Write characters to the console, yielding after each one.
        
        if (ATOMIC_WRITE)
        {
            if (!COMPARE_N_SWAP)
            {
                // Exercise 6:
                // Synchronization mechanism to implement atomic write
                while (atomic_swap(&lock, 1) != 0)  // attempt to acquire the lock
                    continue;
            }
            else
            {
                // Exercise 8:
                // Alternative synchronization mechanism for atomic write
                while (compare_and_swap(&lock, 0, 1) != 0)
                    continue;
            }
        }
        
        // lock acquired
        // atomically print the char
		*cursorpos++ = PRINTCHAR;
        
        if (ATOMIC_WRITE)
        {
            // Exercise 6:
            // release the lock
            atomic_swap(&lock, 0);
        }
        
		sys_yield();
	}
    
	// Yield forever.
	//while (1)
		//sys_yield();
    
    // Exercise 2:
    // Should exit
    sys_exit(0);
}
