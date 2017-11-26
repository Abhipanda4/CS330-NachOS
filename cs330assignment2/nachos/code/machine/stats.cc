// stats.h 
//	Routines for managing statistics about Nachos performance.
//
// DO NOT CHANGE -- these stats are maintained by the machine emulation.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "utility.h"
#include "stats.h"
#include "../threads/system.h"

//----------------------------------------------------------------------
// Statistics::Statistics
// 	Initialize performance metrics to zero, at system startup.
//----------------------------------------------------------------------

Statistics::Statistics()
{
    totalTicks = idleTicks = systemTicks = userTicks = 0;
    numDiskReads = numDiskWrites = 0;
    numConsoleCharsRead = numConsoleCharsWritten = 0;
    numPageFaults = numPacketsSent = numPacketsRecvd = 0;

	// new variables
	cpu_busy_time = 0;
	cpu_util = 0;
	max_cpu_burst = 0;
	min_cpu_burst = 10000000;
	num_cpu_bursts = 0;
	total_wait_time = 0;
	max_thread_completion = 0;
	min_thread_completion = 10000000;
	avg_thread_completion = 0.0;
	var_thread_completion = 0.0;
	burst_estimate_error = 0;
}

//----------------------------------------------------------------------
// Statistics::Print
// 	Print performance metrics, when we've finished everything
//	at system shutdown.
//----------------------------------------------------------------------
void
Statistics::Print()
{
    printf("Ticks: total %d, idle %d, system %d, user %d\n", totalTicks, 
	idleTicks, systemTicks, userTicks);
    printf("Disk I/O: reads %d, writes %d\n", numDiskReads, numDiskWrites);
    printf("Console I/O: reads %d, writes %d\n", numConsoleCharsRead, 
	numConsoleCharsWritten);
    printf("Paging: faults %d\n", numPageFaults);
    printf("Network I/O: packets received %d, sent %d\n", numPacketsRecvd,
	numPacketsSent);

	//----------------------------------------------------------------------
	// print other statistics, including:
	//  -> Total CPU busy time
	//  -> Total Execution time
	//  -> CPU utilization
	//  -> Max, min, avg CPU burst length
	//  -> N of non zero CPU bursts
	//  -> Avg waiting time in ready queue
	//  -> Max, min, avg and variance of thread completion times.
	//----------------------------------------------------------------------

	float avg_cpu_burst = (float)cpu_busy_time / num_cpu_bursts;

	printf("*****************************************\n");
	printf("\nUsing scheduling algorithm: %d\n", algo_num);

	printf("\n<--------REQUIRED PARAMETERS--------->\n\n");
	if (algo_num == PREEMPTIVE_RR || algo_num == PREEMPTIVE_PRIORITY)
		printf("Quanta: %d\n", TimerTicks);
	printf("Total CPU Busy Time: %d\n", cpu_busy_time);
	printf("Total Execution Time: %d\n", totalTicks);
	float cpu_util = 100 * (float)cpu_busy_time / totalTicks;
	printf("CPU Utilization: %f\n\n", cpu_util);
	printf("Number of non zero CPU bursts: %d\n", num_cpu_bursts);
	printf("CPU BURST TIMES:\n");
	printf("\t->Maximum: %d\n", max_cpu_burst);
	printf("\t->Minimum: %d\n", min_cpu_burst);
	printf("\t->Average: %f\n\n", avg_cpu_burst);
	if (algo_num == NON_PREEMPTIVE_SJF) {
		printf("Burst Estimate Error: %f\n", (float)burst_estimate_error / cpu_busy_time);
	}
	if (thread_index > 1) {
		printf("Average waiting time in ready Queue: %f\n", (float)total_wait_time / thread_index);
		printf("THREAD COMPLETION TIMES:\n");
		printf("\t->Maximum: %d\n", max_thread_completion);
		printf("\t->Minimum: %d\n", min_thread_completion);
		printf("\t->Average: %f\n", avg_thread_completion);
		printf("\t->Variance: %f\n", var_thread_completion);
	} else {
		printf("Only main thread was running\n");
	}
}
