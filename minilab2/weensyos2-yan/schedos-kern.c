#include "schedos-kern.h"
#include "x86.h"
#include "lib.h"
#include "ma_rand.h" // Exercise 7: random number generator for the lottery scheduling

// Exercise: set the preferred algorithm number here
// because it is more convenient
#ifndef ALGO_NO
#define ALGO_NO 0
#endif

/*****************************************************************************
 * schedos-kern
 *
 *   This is the schedos's kernel.
 *   It sets up process descriptors for the 4 applications, then runs
 *   them in some schedule.
 *
 *****************************************************************************/

// The program loader loads 4 processes, starting at PROC1_START, allocating
// 1 MB to each process.
// Each process's stack grows down from the top of its memory space.
// (But note that SchedOS processes, like MiniprocOS processes, are not fully
// isolated: any process could modify any part of memory.)

#define NPROCS		5
#define PROC1_START	0x200000
#define PROC_SIZE	0x100000

// +---------+-----------------------+--------+---------------------+---------/
// | Base    | Kernel         Kernel | Shared | App 0         App 0 | App 1
// | Memory  | Code + Data     Stack | Data   | Code + Data   Stack | Code ...
// +---------+-----------------------+--------+---------------------+---------/
// 0x0    0x100000               0x198000 0x200000              0x300000
//
// The program loader puts each application's starting instruction pointer
// at the very top of its stack.
//
// System-wide global variables shared among the kernel and the four
// applications are stored in memory from 0x198000 to 0x200000.  Currently
// there is just one variable there, 'cursorpos', which occupies the four
// bytes of memory 0x198000-0x198003.  You can add more variables by defining
// their addresses in schedos-symbols.ld; make sure they do not overlap!


// A process descriptor for each process.
// Note that proc_array[0] is never used.
// The first application process descriptor is proc_array[1].
static process_t proc_array[NPROCS];

// A pointer to the currently running process.
// This is kept up to date by the run() function, in mpos-x86.c.
process_t *current;

// The preferred scheduling algorithm.
int scheduling_algorithm;

// Exercise 4A:
// last process pid
pid_t last_pid;

/*****************************************************************************
 * start
 *
 *   Initialize the hardware and process descriptors, then run
 *   the first process.
 *
 *****************************************************************************/

void
start(void)
{
	int i;

	// Set up hardware (schedos-x86.c)
	segments_init();
	interrupt_controller_init(0);
	console_clear();

	// Initialize process descriptors as empty
	memset(proc_array, 0, sizeof(proc_array));
	for (i = 0; i < NPROCS; i++) {
		proc_array[i].p_pid = i;
		proc_array[i].p_state = P_EMPTY;
        proc_array[i].p_priority = 1;   // Exercise 4A: default priority level = 1
        proc_array[i].p_share = 1;      // Exercise 4B: default share = 1
        proc_array[i].p_run_count = 0;  // Exercise 4B: default run count = 0
	}

	// Set up process descriptors (the proc_array[])
	for (i = 1; i < NPROCS; i++) {
		process_t *proc = &proc_array[i];
		uint32_t stack_ptr = PROC1_START + i * PROC_SIZE;

		// Initialize the process descriptor
		special_registers_init(proc);

		// Set ESP
		proc->p_registers.reg_esp = stack_ptr;

		// Load process and set EIP, based on ELF image
		program_loader(i - 1, &proc->p_registers.reg_eip);

        //proc->p_priority = 1;   // Exercise 4A: default priority level = 1
        //proc->p_share = 1;      // Exercise 4B: default share = 1
        //proc->p_run_count = 0;
        
		// Mark the process as runnable!
		proc->p_state = P_RUNNABLE;
	}

	// Initialize the cursor-position shared variable to point to the
	// console's first character (the upper left).
	cursorpos = (uint16_t *) 0xB8000;
    
    // Exercise 4A:
    last_pid = 0;
    
    // Exercise 6:
    // Initialize the lock to be unlocked
    lock = 0;

	// Initialize the scheduling algorithm.
	scheduling_algorithm = ALGO_NO;

	// Switch to the first process.
	run(&proc_array[1]);

	// Should never get here!
	while (1)
		/* do nothing */;
}



/*****************************************************************************
 * interrupt
 *
 *   This is the weensy interrupt and system call handler.
 *   The current handler handles 4 different system calls (two of which
 *   do nothing), plus the clock interrupt.
 *
 *   Note that we will never receive clock interrupts while in the kernel.
 *
 *****************************************************************************/

void
interrupt(registers_t *reg)
{
	// Save the current process's register state
	// into its process descriptor
	current->p_registers = *reg;

	switch (reg->reg_intno) {

	case INT_SYS_YIELD:
		// The 'sys_yield' system call asks the kernel to schedule
		// the next process.
		schedule();

	case INT_SYS_EXIT:
		// 'sys_exit' exits the current process: it is marked as
		// non-runnable.
		// The application stored its exit status in the %eax register
		// before calling the system call.  The %eax register has
		// changed by now, but we can read the application's value
		// out of the 'reg' argument.
		// (This shows you how to transfer arguments to system calls!)
		current->p_state = P_ZOMBIE;
		current->p_exit_status = reg->reg_eax;
		schedule();

	case INT_SYS_USER1:
		// 'sys_user*' are provided for your convenience, in case you
		// want to add a system call.
		/* Your code here (if you want). */
            
        // Exercise 4A:
        // Set priority level
        current->p_priority = reg->reg_eax;
        schedule();
		//run(current);

	case INT_SYS_USER2:
		/* Your code here (if you want). */
            
        current->p_share = reg->reg_eax;
        schedule();
        //run(current);
            
    case INT_CLOCK:
		// A clock interrupt occurred (so an application exhausted its
		// time quantum).
		// Switch to the next runnable process.
		schedule();

	default:
		while (1)
			/* do nothing */;

	}
}



/*****************************************************************************
 * schedule
 *
 *   This is the weensy process scheduler.
 *   It picks a runnable process, then context-switches to that process.
 *   If there are no runnable processes, it spins forever.
 *
 *   This function implements multiple scheduling algorithms, depending on
 *   the value of 'scheduling_algorithm'.  We've provided one; in the problem
 *   set you will provide at least one more.
 *
 *****************************************************************************/

void
schedule(void)
{
	pid_t pid = current->p_pid;
    
    if (scheduling_algorithm == 0)
        while (1) {
			pid = (pid + 1) % NPROCS;

			// Run the selected process, but skip
			// non-runnable processes.
			// Note that the 'run' function does not return.
			if (proc_array[pid].p_state == P_RUNNABLE)
				run(&proc_array[pid]);
		}
    else if (scheduling_algorithm == 1)
    {
        // Exercise 2:
        // Strict priority scheduling
        // smaller pid -> higher priority
        int i;
        pid_t highest_priority_pid = 0;
        while (1)
        {
            for (i = 1; i < NPROCS; i++)
            {
                if (proc_array[i].p_state == P_RUNNABLE)
                {
                    highest_priority_pid = i;
                    break;
                }
            }
            if (highest_priority_pid != 0)
                run(&proc_array[highest_priority_pid]);
        }
    }
    else if (scheduling_algorithm == 2)
    {
        // Exercise 4A:
        // User-defined priority scheduling
        // smaller priotity levels -> higher priority
        int i;
        pid_t highest_priority_pid = 0;
        
        while (1)
        {
            for (i = 1; i < NPROCS; i++)
            {
                if (proc_array[i].p_state == P_RUNNABLE)
                {
                    if (highest_priority_pid == 0)
                    {
                        // picks the first runnable process as the default process
                        highest_priority_pid = i;
                    }
                    else
                    {
                        if (proc_array[i].p_priority < proc_array[highest_priority_pid].p_priority)
                            highest_priority_pid = i;
                    }
                }
                
                
            }
            
            if (highest_priority_pid != 0)
            {
                if (last_pid != 0 && last_pid < NPROCS - 1 && proc_array[last_pid].p_priority == proc_array[highest_priority_pid].p_priority)
                {
                    // check if there is another process with the same priority
                    // as the currently chosen one.
                    // This allows the scheduler to alternate between processes with the same priority.
                    for (i = last_pid + 1; i < NPROCS; i++)
                    {
                        if (proc_array[i].p_state == P_RUNNABLE)
                        {
                            if (proc_array[i].p_priority == proc_array[highest_priority_pid].p_priority)
                            {
                                last_pid = i;
                                run(&proc_array[i]);
                            }
                        }
                    }
                }
                
                // last_pid helps us know whether to allow a process to run again
                // if there is another process with the same priority
                // Scheduler will let another process with the same priority to run
                // if the currently chosen process has just run
                last_pid = highest_priority_pid;
                run(&proc_array[highest_priority_pid]);
            }
        }
    }
    else if (scheduling_algorithm == 3)
    {
        // Exercise 4B:
        // User-defined proportional-share scheduling
        // larger shares -> higher priority
        int i;
        pid_t highest_priority_pid = 0;
        pid_t lowest_priority_pid = 0;  // used to know whether all processes have used their shares
        
        while (1)
        {
            for (i = 1; i < NPROCS; i++)
            {
                if (proc_array[i].p_state == P_RUNNABLE)
                {
                    if (highest_priority_pid == 0)
                    {
                        // picks the first runnable process as the default process
                        highest_priority_pid = i;
                        lowest_priority_pid = i;
                    }
                    else
                    {
                        // check if the process has run as many times as its share
                        if (proc_array[i].p_run_count < proc_array[i].p_share)
                        {
                            if (proc_array[i].p_share > proc_array[highest_priority_pid].p_share)
                            {
                                // process with the larger share should go first
                                highest_priority_pid = i;
                            }
                            else
                            {
                                lowest_priority_pid = i;
                                
                                if (proc_array[highest_priority_pid].p_run_count == proc_array[highest_priority_pid].p_share)
                                    // the currently chosen process should go only
                                    // if the previously chosen process has run as
                                    // many times as its share
                                    highest_priority_pid = i;
                            }
                        }
                    }
                }
            }
            
            if (highest_priority_pid != 0)
            {
                proc_array[highest_priority_pid].p_run_count++; // keep track of run
                
                if (highest_priority_pid == lowest_priority_pid && proc_array[highest_priority_pid].p_run_count == proc_array[highest_priority_pid].p_share)
                {
                    for (i = 1; i < NPROCS; i++)
                    {
                        if (proc_array[i].p_state == P_RUNNABLE)
                        {
                            // reset all the runnable processes' run count back to 0
                            // so the processes can run again as many times as their shares
                            proc_array[i].p_run_count = 0;
                        }
                    }
                }
                
                run(&proc_array[highest_priority_pid]);
            }
        }
    }
    else if (scheduling_algorithm == 4)
    {
        // Exercise 7:
        // Lottery Scheduling
        // ticket = pid
        // each process has one ticket which is its pid
        int random;
        int total_tickets = NPROCS - 1;
        
        do
        {
            // pick ticket (or pid) randomly
            // using the random number generator 'ma_rand'
            // from ma_rand.c
            random = (ma_rand() % total_tickets) + 1;
            
        } while (proc_array[random].p_state != P_RUNNABLE);
        
        run(&proc_array[random]);
    }

	// If we get here, we are running an unknown scheduling algorithm.
	cursorpos = console_printf(cursorpos, 0x100, "\nUnknown scheduling algorithm %d\n", scheduling_algorithm);
	while (1)
		/* do nothing */;
}
