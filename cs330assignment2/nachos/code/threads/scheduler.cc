// scheduler.cc
//	Routines to choose the next thread to run, and to dispatch to
//	that thread.
//
// 	These routines assume that interrupts are already disabled.
//	If interrupts are disabled, we can assume mutual exclusion
//	(since we are on a uniprocessor).
//
// 	NOTE: We can't use Locks to provide mutual exclusion here, since
// 	if we needed to wait for a lock, and the lock was busy, we would
//	end up calling SelectNextReadyThread(), and that would put us in an
//	infinite loop.
//
// 	Very simple implementation -- no priorities, straight FIFO.
//	Might need to be improved in later assignments.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "scheduler.h"
#include "system.h"

//----------------------------------------------------------------------
// ProcessScheduler::ProcessScheduler
// 	Initialize the list of ready but not running threads to empty.
//----------------------------------------------------------------------

ProcessScheduler::ProcessScheduler()
{
	listOfReadyThreads = new List;
}

//----------------------------------------------------------------------
// ProcessScheduler::~ProcessScheduler
// 	De-allocate the list of ready threads.
//----------------------------------------------------------------------

ProcessScheduler::~ProcessScheduler()
{
	delete listOfReadyThreads;
}

//----------------------------------------------------------------------
// ProcessScheduler::MoveThreadToReadyQueue
// 	Mark a thread as ready, but not running.
//	Put it on the ready list, for later scheduling onto the CPU.
//
//	"thread" is the thread to be put on the ready list.
//----------------------------------------------------------------------

//TODO: Update priority of the threads based on algo useed for scheduling
void
ProcessScheduler::MoveThreadToReadyQueue (NachOSThread *thread)
{
	DEBUG('t', "Putting thread %s with PID %d on ready list.\n", thread->getName(), thread->GetPID());

	if (thread->status == RUNNING) {
		int run_time = stats->totalTicks - cpu_burst_start;
		stats->cpu_busy_time += run_time;
		if (run_time > 0) {
			stats->num_cpu_bursts++;
			if (run_time > stats->max_cpu_burst)
				stats->max_cpu_burst = run_time;
			if (run_time < stats->min_cpu_burst)
				stats->min_cpu_burst = run_time;
			if (algo_num == PREEMPTIVE_PRIORITY) {
				UpdateUsageAndPriority(run_time);
			}
			if (algo_num == NON_PREEMPTIVE_SJF) {
				stats->burst_estimate_error += abs(stats->totalTicks - cpu_burst_start - thread->threadPriority);
				thread->threadPriority = int(ALPHA * run_time + (1 - ALPHA) * thread->threadPriority);
			}
		}
		//printf("----------%d\n", thread->get_wait_time_start());
		//thread->set_wait_time_start(stats->totalTicks);
	}
	thread->setStatus(READY);
	// wait time start reinitialize; irrespective of thread's previous status
	thread->set_wait_time_start(stats->totalTicks);
	listOfReadyThreads->Append((void *)thread);
}

void ProcessScheduler::UpdateUsageAndPriority(int run_time) {
	int recent_cpu_usage;
	// Update the usage & priority of all threads
	int i;
	for (i = 0; i < thread_index; i++) {
		if (i == currentThread->GetPID()) {
			recent_cpu_usage = (currentThread->cpu_usage + run_time) / 2;
			threadArray[i]->cpu_usage = recent_cpu_usage;
			threadArray[i]->threadPriority = threadArray[i]->basePriority + recent_cpu_usage/2;
		} else if (!exitThreadArray[i]) {
			recent_cpu_usage = threadArray[i]->cpu_usage;
			threadArray[i]->cpu_usage = recent_cpu_usage/2;
			threadArray[i]->threadPriority = threadArray[i]->basePriority + recent_cpu_usage/2;
		}
	}
}

//----------------------------------------------------------------------
// ProcessScheduler::SelectNextReadyThread
// 	Return the next thread to be scheduled onto the CPU.
//	If there are no ready threads, return NULL.
// Side effect:
//	Thread is removed from the ready list.
//----------------------------------------------------------------------

	NachOSThread *
ProcessScheduler::SelectNextReadyThread ()
{
	// Implemented the various scheduling algorithm here
	if (algo_num == NON_PREEMPTIVE_FCFS) {
		return (NachOSThread *)listOfReadyThreads->Remove();
	} else if (algo_num == NON_PREEMPTIVE_SJF) {
		return (NachOSThread *)listOfReadyThreads->getMaxPriorityThread();
	} else if (algo_num == PREEMPTIVE_RR) {
		return (NachOSThread *)listOfReadyThreads->Remove();
	} else if (algo_num == PREEMPTIVE_PRIORITY) {
		return (NachOSThread *)listOfReadyThreads->getMaxPriorityThread();
	}
}

//----------------------------------------------------------------------
// ProcessScheduler::ScheduleThread
// 	Dispatch the CPU to nextThread.  Save the state of the old thread,
//	and load the state of the new thread, by calling the machine
//	dependent context switch routine, SWITCH.
//
//      Note: we assume the state of the previously running thread has
//	already been changed from running to blocked or ready (depending).
// Side effect:
//	The global variable currentThread becomes nextThread.
//
//	"nextThread" is the thread to be put into the CPU.
//----------------------------------------------------------------------

void
ProcessScheduler::ScheduleThread (NachOSThread *nextThread)
{
	NachOSThread *oldThread = currentThread;
	cpu_burst_start = stats->totalTicks;
	//TODO: Waiting time
	//printf("***************%d*****\n", nextThread->get_wait_time_start());
	//printf("*****%d*******\n", stats->totalTicks - nextThread->get_wait_time_start());
	stats->total_wait_time += (stats->totalTicks - nextThread->get_wait_time_start());

#ifdef USER_PROGRAM			// ignore until running user programs
	if (currentThread->space != NULL) {	// if this thread is a user program,
		currentThread->SaveUserState(); // save the user's CPU registers
		currentThread->space->SaveContextOnSwitch();
	}
#endif

	oldThread->CheckOverflow();		    // check if the old thread
	// had an undetected stack overflow

	currentThread = nextThread;		    // switch to the next thread
	currentThread->setStatus(RUNNING);      // nextThread is now running

	DEBUG('t', "Switching from thread \"%s\" with pid %d to thread \"%s\" with pid %d\n",
			oldThread->getName(), oldThread->GetPID(), nextThread->getName(), nextThread->GetPID());

	// This is a machine-dependent assembly language routine defined
	// in switch.s.  You may have to think
	// a bit to figure out what happens after this, both from the point
	// of view of the thread and from the perspective of the "outside world".

	_SWITCH(oldThread, nextThread);

	DEBUG('t', "Now in thread \"%s\" with pid %d\n", currentThread->getName(), currentThread->GetPID());

	// If the old thread gave up the processor because it was finishing,
	// we need to delete its carcass.  Note we cannot delete the thread
	// before now (for example, in NachOSThread::FinishThread()), because up to this
	// point, we were still running on the old thread's stack!
	if (threadToBeDestroyed != NULL) {
		delete threadToBeDestroyed;
		threadToBeDestroyed = NULL;
	}

#ifdef USER_PROGRAM
	if (currentThread->space != NULL) {		// if there is an address space
		currentThread->RestoreUserState();     // to restore, do it.
		currentThread->space->RestoreContextOnSwitch();
	}
#endif
}

//----------------------------------------------------------------------
// ProcessScheduler::Tail
//      This is the portion of ProcessScheduler::ScheduleThread after _SWITCH(). This needs
//      to be executed in the startup function used in fork().
//----------------------------------------------------------------------

	void
ProcessScheduler::Tail ()
{
	// If the old thread gave up the processor because it was finishing,
	// we need to delete its carcass.  Note we cannot delete the thread
	// before now (for example, in NachOSThread::FinishThread()), because up to this
	// point, we were still running on the old thread's stack!
	if (threadToBeDestroyed != NULL) {
		delete threadToBeDestroyed;
		threadToBeDestroyed = NULL;
	}

#ifdef USER_PROGRAM
	if (currentThread->space != NULL) {         // if there is an address space
		currentThread->RestoreUserState();     // to restore, do it.
		currentThread->space->RestoreContextOnSwitch();
	}
#endif
}

//----------------------------------------------------------------------
// ProcessScheduler::Print
// 	Print the scheduler state -- in other words, the contents of
//	the ready list.  For debugging.
//----------------------------------------------------------------------
	void
ProcessScheduler::Print()
{
	printf("Ready list contents:\n");
	listOfReadyThreads->Mapcar((VoidFunctionPtr) ThreadPrint);
}
