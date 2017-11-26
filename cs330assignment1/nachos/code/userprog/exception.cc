// exception.cc 
//	Entry point into the Nachos kernel from user programs.
//	There are two kinds of things that can cause control to
//	transfer back to here from user code:
//
//	syscall -- The user code explicitly requests to call a procedure
//	in the Nachos kernel.  Right now, the only function we support is
//	"Halt".
//
//	exceptions -- The user code does something that the CPU can't handle.
//	For instance, accessing memory that doesn't exist, arithmetic errors,
//	etc.  
//
//	Interrupts (which can also cause control to transfer from user
//	code into the Nachos kernel) are handled elsewhere.
//
// For now, this only handles the Halt() system call.
// Everything else core dumps.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "syscall.h"
#include "console.h"
#include "synch.h"

//----------------------------------------------------------------------
// ExceptionHandler
// 	Entry point into the Nachos kernel.  Called when a user program
//	is executing, and either does a syscall, or generates an addressing
//	or arithmetic exception.
//
// 	For system calls, the following is the calling convention:
//
// 	system call code -- r2
//		arg1 -- r4
//		arg2 -- r5
//		arg3 -- r6
//		arg4 -- r7
//
//	The result of the system call, if any, must be put back into r2. 
//
// And don't forget to increment the pc before returning. (Or else you'll
// loop making the same system call forever!
//
//	"which" is the kind of exception.  The list of possible exceptions 
//	are in machine.h.
//----------------------------------------------------------------------
static Semaphore *readAvail;
static Semaphore *writeDone;
static void ReadAvail(int arg) { readAvail->V(); }
static void WriteDone(int arg) { writeDone->V(); }

#define MAX_LEN 256

// Helper function for fork call
void foo(int arg) {
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

	machine->Run();
}

static void ConvertIntToHex (unsigned v, Console *console)
{
	unsigned x;
	if (v == 0) return;
	ConvertIntToHex (v/16, console);
	x = v % 16;
	if (x < 10) {
		writeDone->P() ;
		console->PutChar('0'+x);
	}
	else {
		writeDone->P() ;
		console->PutChar('a'+x-10);
	}
}

	void
ExceptionHandler(ExceptionType which)
{
	int type = machine->ReadRegister(2);
	int memval, vaddr, printval, tempval, exp;
	unsigned printvalus;        // Used for printing in hex
	if (!initializedConsoleSemaphores) {
		readAvail = new Semaphore("read avail", 0);
		writeDone = new Semaphore("write done", 1);
		initializedConsoleSemaphores = true;
	}
	Console *console = new Console(NULL, NULL, ReadAvail, WriteDone, 0);;

	if ((which == SyscallException) && (type == SysCall_Halt)) {
		DEBUG('a', "Shutdown, initiated by user program.\n");
		interrupt->Halt();
	}
	else if ((which == SyscallException) && (type == SysCall_PrintInt)) {
		printval = machine->ReadRegister(4);
		if (printval == 0) {
			writeDone->P() ;
			console->PutChar('0');
		}
		else {
			if (printval < 0) {
				writeDone->P() ;
				console->PutChar('-');
				printval = -printval;
			}
			tempval = printval;
			exp=1;
			while (tempval != 0) {
				tempval = tempval/10;
				exp = exp*10;
			}
			exp = exp/10;
			while (exp > 0) {
				writeDone->P() ;
				console->PutChar('0'+(printval/exp));
				printval = printval % exp;
				exp = exp/10;
			}
		}
		// Advance program counters.
		machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
		machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
		machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
	}
	else if ((which == SyscallException) && (type == SysCall_PrintChar)) {
		writeDone->P() ;
		console->PutChar(machine->ReadRegister(4));   // echo it!
		// Advance program counters.
		machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
		machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
		machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
	}
	else if ((which == SyscallException) && (type == SysCall_PrintString)) {
		vaddr = machine->ReadRegister(4);
		machine->ReadMem(vaddr, 1, &memval);
		while ((*(char*)&memval) != '\0') {
			writeDone->P() ;
			console->PutChar(*(char*)&memval);
			vaddr++;
			machine->ReadMem(vaddr, 1, &memval);
		}
		// Advance program counters.
		machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
		machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
		machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
	}
	else if ((which == SyscallException) && (type == SysCall_PrintIntHex)) {
		printvalus = (unsigned)machine->ReadRegister(4);
		writeDone->P() ;
		console->PutChar('0');
		writeDone->P() ;
		console->PutChar('x');
		if (printvalus == 0) {
			writeDone->P() ;
			console->PutChar('0');
		}
		else {
			ConvertIntToHex (printvalus, console);
		}
		// Advance program counters.
		machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
		machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
		machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
	} else if ((which == SyscallException) && (type == SysCall_Fork)) {
		// Advance program counters.
		machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
		machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
		machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);

		NachOSThread *newThread = new NachOSThread("Forked Thread");

		ProcessAddressSpace* newSpace = new ProcessAddressSpace();
		newThread->space = newSpace;

		int parentstart = (currentThread->space->KernelPageTable[0].physicalPage) * PageSize;
		int childstart = (newThread->space->KernelPageTable[0].physicalPage) * PageSize;
		int s = currentThread->space->numVirtualPages * PageSize;
		int i = parentstart;
		for (int j = childstart; j < childstart + s; j++) {
			machine->mainMemory[j] = machine->mainMemory[i];
			i += 1;
		}

		machine->WriteRegister(2, 0);
		newThread->SaveUserState();
		machine->WriteRegister(2, newThread->getPID());

		newThread->ThreadFork(&foo, 0);

	} else if ((which == SyscallException) && (type == SysCall_GetReg)) {
		machine->WriteRegister(2, machine->ReadRegister(machine->ReadRegister(4)));
		// Advance program counters.
		machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
		machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
		machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);

	} else if ((which == SyscallException) && (type == SysCall_GetPA)) {
		int pa;
		ExceptionType e = machine->Translate(machine->ReadRegister(4), &pa, 0, 0);
		if (e == NoException) {
			machine->WriteRegister(2, pa);
		} else {
			machine->WriteRegister(2, -1);
		}
		// Advance program counters.
		machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
		machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
		machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);

	} else if ((which == SyscallException) && (type == SysCall_GetPID)) {
		machine->WriteRegister(2, currentThread->getPID());
		// Advance program counters.
		machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
		machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
		machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);

	} else if ((which == SyscallException) && (type == SysCall_GetPPID)) {
		machine->WriteRegister(2, currentThread->getPPID());
		// Advance program counters.
		machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
		machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
		machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);

	} else if ((which == SyscallException) && (type == SysCall_Time)) {
		machine->WriteRegister(2, stats->totalTicks);
		// Advance program counters.
		machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
		machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
		machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);

	} else if ((which == SyscallException) && (type == SysCall_Sleep)) {
		int time = machine->ReadRegister(4);
		if (time != 0) {
			NachOSThread::addToSleepQueue(time + stats->totalTicks, currentThread);
			IntStatus oldLevel = interrupt->SetLevel(IntOff);
			currentThread->PutThreadToSleep();
			(void) interrupt->SetLevel(oldLevel);
		} else {
			IntStatus oldLevel = interrupt->SetLevel(IntOff);
			currentThread->YieldCPU();
			(void) interrupt->SetLevel(oldLevel);
		}
		// Advance program counters.
		machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
		machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
		machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);

	} else if ((which == SyscallException) && (type == SysCall_Yield)) {
		currentThread->YieldCPU();
		// Advance program counters.
		machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
		machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
		machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);

	} else if ((which == SyscallException) && (type == SysCall_Exec)) {
		char filename[MAX_LEN];
		// start virt address of the filename
		unsigned int virtualAddr = machine->ReadRegister(4);
		int val;
		int i = 0;
		machine->ReadMem(virtualAddr, 1, &val);
		while (val != 0) {
			// val stores first character in int type
			// convert into char
			filename[i] = (char)val;
			i++;
			virtualAddr++;
			machine->ReadMem(virtualAddr, 1, &val);
		}
		// Terminate the filename with '\0'
		filename[i] = (char)val;

		// filename char array now stores the executable
		// Following code Copied from userprog/progtest.cc
		// Suitable offset changes made in ProcessAddressSpace constructor
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
		ASSERT(FALSE);			// machine->Run
	} else if ((which == SyscallException) && (type == SysCall_NumInstr)) {
		// no of user instructions
		machine->WriteRegister(2, currentThread->numInstr);
		// Advance program counters.
		machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
		machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
		machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);

	} else if ((which == SyscallException) && (type == SysCall_Join)) {
		// Allow thread to join only with its child
		int targetPID = machine->ReadRegister(4);
		// Search for this PID in childList
		childList* child = currentThread->findChild(targetPID);
		if (child == NULL) {
			machine->WriteRegister(2, -1);
		} else {
			// implies child with the targetPID belongs to currentThread
			if (!child->isAlive) {
				// if child has already exited
				// return the exit code of the child
				machine->WriteRegister(2, child->exitstatus);
			} else {
				// child has not exited yet
				IntStatus oldLevel = interrupt->SetLevel(IntOff);
				child->isParentWaiting = true;
				currentThread->PutThreadToSleep();
				(void) interrupt->SetLevel(oldLevel);
			}
			// Check whether the child that woke up the parent thread is
			// the same child who put it to sleep
			childList* child = currentThread->findChild(targetPID);
			while (child->isAlive) {
				IntStatus oldLevel = interrupt->SetLevel(IntOff);
				currentThread->PutThreadToSleep();
				(void) interrupt->SetLevel(oldLevel);
			}
		}
		machine->WriteRegister(2, child->exitstatus);
		// Advance program counters.
		machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
		machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
		machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);

	} else if ((which == SyscallException) && (type == SysCall_Exit)) {
		int exitstatus = machine->ReadRegister(4);
		if (NachOSThread::totalActiveThreads == 1) {
			interrupt->Halt();
		}
		if (currentThread->parent != NULL) {
			childList* targetChild = currentThread->parent->findChild(currentThread->getPID());
			targetChild->exitstatus = exitstatus;
			if (targetChild->isParentWaiting) {
				IntStatus oldLevel = interrupt->SetLevel(IntOff);
				scheduler->MoveThreadToReadyQueue(currentThread->parent);
				(void) interrupt->SetLevel(oldLevel);
			}
			targetChild->isAlive = false;
		}
		// make parent of all its children equal to NULL
		childList* children = currentThread->children;
		while(children != NULL) {
			children->child->parent = NULL;
		}
		currentThread->FinishThread();
	} else {
		printf("Unexpected user mode exception %d %d\n", which, type);
		ASSERT(FALSE);
	}
}
