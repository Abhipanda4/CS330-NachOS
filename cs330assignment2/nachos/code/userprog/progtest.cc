// progtest.cc
//	Test routines for demonstrating that Nachos can load
//	a user program and execute it.
//
//	Also, routines for testing the Console hardware device.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "console.h"
#include "addrspace.h"
#include "synch.h"

//----------------------------------------------------------------------
// LaunchUserProcess
// 	Run a user program.  Open the executable, load it into
//	memory, and jump to it.
//----------------------------------------------------------------------

void
LaunchUserProcess(char *filename)
{
    OpenFile *executable = fileSystem->Open(filename);
    ProcessAddressSpace *space;

    if (executable == NULL) {
	printf("Unable to open file %s\n", filename);
	return;
    }
    space = new ProcessAddressSpace(executable);
    currentThread->space = space;

    delete executable;			// close file

    space->InitUserModeCPURegisters();		// set the initial register values
    space->RestoreContextOnSwitch();		// load page table register

    machine->Run();			// jump to the user progam
    ASSERT(FALSE);			// machine->Run never returns;
					// the address space exits
					// by doing the syscall "exit"
}

// Data structures needed for the console test.  Threads making
// I/O requests wait on a Semaphore to delay until the I/O completes.

static Console *console;
static Semaphore *readAvail;
static Semaphore *writeDone;

//----------------------------------------------------------------------
// ConsoleInterruptHandlers
// 	Wake up the thread that requested the I/O.
//----------------------------------------------------------------------

static void ReadAvail(int arg) { readAvail->V(); }
static void WriteDone(int arg) { writeDone->V(); }

//----------------------------------------------------------------------
// ConsoleTest
// 	Test the console by echoing characters typed at the input onto
//	the output.  Stop when the user types a 'q'.
//----------------------------------------------------------------------

void
ConsoleTest (char *in, char *out)
{
    char ch;

    console = new Console(in, out, ReadAvail, WriteDone, 0);
    readAvail = new Semaphore("read avail", 0);
    writeDone = new Semaphore("write done", 0);

    for (;;) {
	readAvail->P();		// wait for character to arrive
	ch = console->GetChar();
	console->PutChar(ch);	// echo it!
	writeDone->P() ;        // wait for write to finish
	if (ch == 'q') return;  // if q, quit
    }
}


//----------------------------------------------------------------------
// CUSTOM FUNCTION
// function to be called at the start of the Thread
//----------------------------------------------------------------------
void start_func(int t) {
	// scheduler->tail fn called in Startup()
	// Needs to be executed in startup
	currentThread->Startup();
	machine->Run();
}

//----------------------------------------------------------------------
// CUSTOM FUNCTION
// Reads a  file in the format of:
// executable_name priority_value(OPTIONAL)
// if priority_value is absent, 100 is assigned by default
//----------------------------------------------------------------------
void ReadInputFile(char* filename) {
	OpenFile* fp = fileSystem->Open(filename);
	char c;
	if (fp == NULL) {
		printf("No such file exists....\n");
		return;
	} else {
		// Read the algorithm type first
		printf("Reading File\n");
		fp->Read(&c, 1);
		algo_num = c - '0';
		//printf("%d\n", algo_num);

		// Now read the file line by line
		int MAX_LEN = 128;
		int MAX_BATCH_SIZE = 10;
		int DEFAULT_PRIORITY = 100;
		char** processList;
		processList = new char*[MAX_BATCH_SIZE];
		int j = 0;
		for (j = 0; j < MAX_BATCH_SIZE; j++) {
			processList[j] = new char[MAX_LEN];
		}
		int priorityList[MAX_BATCH_SIZE];
		int processNum = 0;
		int i = 0;

		// First read the \n at end of algo_num
		fp->Read(&c, 1);
		int bytesRead = fp->Read(&c, 1);
		while(bytesRead != 0) {
			// continue reading until space or \n is reached
			while (c != ' ' && c != '\n') {
				processList[processNum][i] = c;
				i++;
				bytesRead = fp->Read(&c, 1);
			}
			processList[processNum][i] == '\0';
			if (c == '\n') {
				// if \n is encountered first => assign default priority value
				priorityList[processNum] = DEFAULT_PRIORITY;
			} else if (c == ' ') {
				 //' ' space is encountered
				 //=> read the priority value
				int tmp = 0;
				bytesRead = fp->Read(&c, 1);
				while(c != '\n') {
					tmp = 10 * tmp + c - '0';
					bytesRead = fp->Read(&c, 1);
				}
				priorityList[processNum] = tmp;
			}
			//printf("%s\n", processList[processNum]);
			processNum++;
			i = 0;
			bytesRead = fp->Read(&c, 1);
		}
		delete fp;
		// Create one thread for each executable with the corresponding priority value
		// And enque it to the ReadyQueue
		i = 0;
		char name[16];
		//printf("%d\n\n", processNum);
		while(i < processNum) {
			// Process-name: processList[i]
			// priority-value: priorityList[i]
			fp = fileSystem->Open(processList[i]);
			if (fp == NULL) {
				return;
			}
			sprintf(name, "Process_%d", (i+1));

			// Setup the new process
			NachOSThread* newProcess = new NachOSThread(name, priorityList[i]);
			newProcess->space = new ProcessAddressSpace(fp);
			newProcess->space->InitUserModeCPURegisters();
			newProcess->SaveUserState();
			newProcess->CreateThreadStack(start_func, 0);
			//printf("<---%s---%d--->\n",newProcess->getName(), newProcess->GetPID());

			// Enque in ready queue
			newProcess->Schedule();
			i++;
			delete fp;
		}
		exitThreadArray[currentThread->GetPID()] = true;

		// Call Exit
		// parameter is a boolean which specifies if all threas have calleed Exit
		for (i = 0; i < thread_index; i++) {
			if (!exitThreadArray[i])
				break;
		}
		bool terminateSim = false;
		if (i == thread_index) terminateSim = true;
		currentThread->Exit(terminateSim, 0);
	}
}
