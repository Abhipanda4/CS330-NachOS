// addrspace.cc 
//	Routines to manage address spaces (executing user programs).
//
//	In order to run a user program, you must:
//
//	1. link with the -N -T 0 option 
//	2. run coff2noff to convert the object file to Nachos format
//		(Nachos object code format is essentially just a simpler
//		version of the UNIX executable object code format)
//	3. load the NOFF file into the Nachos file system
//		(if you haven't implemented the file system yet, you
//		don't need to do this last step)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "addrspace.h"

//----------------------------------------------------------------------
// SwapHeader
// 	Do little endian to big endian conversion on the bytes in the 
//	object file header, in case the file was generated on a little
//	endian machine, and we're now running on a big endian machine.
//----------------------------------------------------------------------

  static void 
SwapHeader (NoffHeader *noffH)
{
  noffH->noffMagic = WordToHost(noffH->noffMagic);
  noffH->code.size = WordToHost(noffH->code.size);
  noffH->code.virtualAddr = WordToHost(noffH->code.virtualAddr);
  noffH->code.inFileAddr = WordToHost(noffH->code.inFileAddr);
  noffH->initData.size = WordToHost(noffH->initData.size);
  noffH->initData.virtualAddr = WordToHost(noffH->initData.virtualAddr);
  noffH->initData.inFileAddr = WordToHost(noffH->initData.inFileAddr);
  noffH->uninitData.size = WordToHost(noffH->uninitData.size);
  noffH->uninitData.virtualAddr = WordToHost(noffH->uninitData.virtualAddr);
  noffH->uninitData.inFileAddr = WordToHost(noffH->uninitData.inFileAddr);
}

//----------------------------------------------------------------------
// ProcessAddressSpace::ProcessAddressSpace
// 	Create an address space to run a user program.
//	Load the program from a file "executable", and set everything
//	up so that we can start executing user instructions.
//
//	Assumes that the object code file is in NOFF format.
//
//	First, set up the translation from program memory to physical 
//	memory.  For now, this is really simple (1:1), since we are
//	only uniprogramming, and we have a single unsegmented page table
//
//	"executable" is the file containing the object code to load into memory
//----------------------------------------------------------------------

ProcessAddressSpace::ProcessAddressSpace(OpenFile *executable, char* f, int pid)
{
  calling_PID = pid;
  unsigned int i, size;
  unsigned vpn, offset;
  TranslationEntry *entry;
  unsigned int pageFrame;

  fileName = new char[1024];

  for (int c = 0; c < 1024; c++) {
    fileName[c] = f[c];
    if (f[c] == '\0') {
      break;
    }
  }

  executable->ReadAt((char *)&noffH, sizeof(noffH), 0);
  if ((noffH.noffMagic != NOFFMAGIC) && 
      (WordToHost(noffH.noffMagic) == NOFFMAGIC))
    SwapHeader(&noffH);
  ASSERT(noffH.noffMagic == NOFFMAGIC);

  // how big is address space?
  size = noffH.code.size + noffH.initData.size + noffH.uninitData.size 
    + UserStackSize;	// we need to increase the size
  // to leave room for the stack
  numVirtualPages = divRoundUp(size, PageSize);
  size = numVirtualPages * PageSize;
  backup_array = new char[size];
  bzero(backup_array, size);

  // first, set up the translation 
  KernelPageTable = new TranslationEntry[numVirtualPages];
  for (i = 0; i < numVirtualPages; i++) {
    KernelPageTable[i].virtualPage = i;
    KernelPageTable[i].shared = FALSE;
    KernelPageTable[i].physicalPage = -1;
    KernelPageTable[i].valid = FALSE;
    KernelPageTable[i].use = FALSE;
    KernelPageTable[i].backup = FALSE;
    KernelPageTable[i].dirty = FALSE;
    KernelPageTable[i].readOnly = FALSE;  // if the code segment was entirely on 
    // a separate page, we could set its 
    // pages to be read-only
  }
  // zero out the entire address space, to zero the unitialized data segment 
  // and the stack segment
  DEBUG('a', "Initializing address space, num pages %d, size %d\n", 
      numVirtualPages, size);
}

//----------------------------------------------------------------------
// ProcessAddressSpace::ProcessAddressSpace (ProcessAddressSpace*) is called by a forked thread.
//      We need to duplicate the address space of the parent.
//----------------------------------------------------------------------

ProcessAddressSpace::ProcessAddressSpace(ProcessAddressSpace *parentSpace, int pid)
{
  calling_PID = pid;
  numVirtualPages = parentSpace->GetNumPages();
  unsigned i, size = numVirtualPages * PageSize;

  // ASSERT(numVirtualPages+numPagesAllocated <= NumPhysPages);                // check we're not trying
  // to run anything too big --
  // at least until we have
  // virtual memory

  DEBUG('a', "Initializing address space, num pages %d, size %d\n",
      numVirtualPages, size);
  // first, set up the translation
  TranslationEntry* parentPageTable = parentSpace->GetPageTable();
  backup_array = new char[size];
  bzero(backup_array, size);

  fileName = new char[1024];
  for (int c = 0; c < 1024; c++) {
    fileName[c] = parentSpace->fileName[c];
    if (parentSpace->fileName[c] == '\0') {
      break;
    }
  }
  noffH = parentSpace->noffH;

  int newAssigned = 0;
  KernelPageTable = new TranslationEntry[numVirtualPages];
  for (i = 0; i < numVirtualPages; i++) {
    KernelPageTable[i].virtualPage = i;
    KernelPageTable[i].shared = parentPageTable[i].shared;

    if (parentPageTable[i].shared) {
      KernelPageTable[i].physicalPage = parentPageTable[i].physicalPage;
    } else {
      if (parentPageTable[i].valid) {
        if (numPagesAllocated == NumPhysPages)
          KernelPageTable[i].physicalPage = getNextPhysicalPage(i, true, parentPageTable[i].physicalPage);
        else
          KernelPageTable[i].physicalPage = getNextPhysicalPage(i, false, parentPageTable[i].physicalPage);
        newAssigned += 1;
      } else {
        KernelPageTable[i].physicalPage = -1;
      }
    }

    KernelPageTable[i].valid = parentPageTable[i].valid;
    KernelPageTable[i].use = parentPageTable[i].use;
    KernelPageTable[i].backup = parentPageTable[i].backup;
    KernelPageTable[i].dirty = parentPageTable[i].dirty;
    KernelPageTable[i].readOnly = parentPageTable[i].readOnly;  	// if the code segment was entirely on
    // a separate page, we could set its
    // pages to be read-only
  }

  // Copy the contents
  unsigned startAddrParent = parentPageTable[0].physicalPage*PageSize;
  unsigned startAddrChild = (numPagesAllocated - newAssigned)*PageSize;
  unsigned int j = 0;
  for (j=0; j < numVirtualPages;j++) {
    if ((parentPageTable[j].shared == FALSE) && (parentPageTable[j].valid == TRUE)) {
      startAddrParent = parentPageTable[j].physicalPage*PageSize;
      startAddrChild = KernelPageTable[j].physicalPage*PageSize;
      for (i=0; i<PageSize; i++) {
        machine->mainMemory[startAddrChild+i] = machine->mainMemory[startAddrParent+i];
      }
    }
  }
  memcpy(backup_array, parentSpace->backup_array, numVirtualPages * PageSize);
}

//----------------------------------------------------------------------
// ProcessAddressSpace::~ProcessAddressSpace
// 	Dealloate an address space.  Some(No)thing for now!
//----------------------------------------------------------------------

ProcessAddressSpace::~ProcessAddressSpace()
{
  int i;
  for (i = 0; i < numVirtualPages; i++) {
    if (KernelPageTable[i].valid && !KernelPageTable[i].shared) {
      // page is not shared but valid, remove it
      int tmp = KernelPageTable[i].physicalPage;
      machine->PhysToVirtual[tmp] = -1;
      machine->PIDatPhysAddr[tmp] = -1;
      numPagesAllocated -= 1;
    }
  }
  delete[] backup_array;
  delete KernelPageTable;
}

//----------------------------------------------------------------------
// ProcessAddressSpace::InitUserModeCPURegisters
// 	Set the initial values for the user-level register set.
//
// 	We write these directly into the "machine" registers, so
//	that we can immediately jump to user code.  Note that these
//	will be saved/restored into the currentThread->userRegisters
//	when this thread is context switched out.
//----------------------------------------------------------------------

  void
ProcessAddressSpace::InitUserModeCPURegisters()
{
  int i;

  for (i = 0; i < NumTotalRegs; i++)
    machine->WriteRegister(i, 0);

  // Initial program counter -- must be location of "Start"
  machine->WriteRegister(PCReg, 0);	

  // Need to also tell MIPS where next instruction is, because
  // of branch delay possibility
  machine->WriteRegister(NextPCReg, 4);

  // Set the stack register to the end of the address space, where we
  // allocated the stack; but subtract off a bit, to make sure we don't
  // accidentally reference off the end!
  machine->WriteRegister(StackReg, numVirtualPages * PageSize - 16);
  DEBUG('a', "Initializing stack register to %d\n", numVirtualPages * PageSize - 16);
}

//----------------------------------------------------------------------
// ProcessAddressSpace::SaveContextOnSwitch
// 	On a context switch, save any machine state, specific
//	to this address space, that needs saving.
//
//	For now, nothing!
//----------------------------------------------------------------------

void ProcessAddressSpace::SaveContextOnSwitch() 
{}

//----------------------------------------------------------------------
// ProcessAddressSpace::RestoreContextOnSwitch
// 	On a context switch, restore the machine state so that
//	this address space can run.
//
//      For now, tell the machine where to find the page table.
//----------------------------------------------------------------------

void ProcessAddressSpace::RestoreContextOnSwitch() 
{
  machine->KernelPageTable = KernelPageTable;
  machine->KernelPageTableSize = numVirtualPages;
  DEBUG('t', "Size: %d\n", numVirtualPages);
}

  unsigned
ProcessAddressSpace::GetNumPages()
{
  return numVirtualPages;
}

  TranslationEntry*
ProcessAddressSpace::GetPageTable()
{
  return KernelPageTable;
}

unsigned int
ProcessAddressSpace::addSharedMemory(unsigned int newMemory) {

  unsigned int numNewPages = 1 + ((newMemory - 1) / PageSize);
  unsigned numTotalPages = numVirtualPages + numNewPages;
  unsigned int i;

  DEBUG('z', "Added %d Pages\n", numNewPages);

  TranslationEntry *NewKernelPageTable = new TranslationEntry[numTotalPages];

  for (i = 0; i < numVirtualPages; i++) {
    NewKernelPageTable[i].virtualPage = KernelPageTable[i].virtualPage;
    NewKernelPageTable[i].shared = KernelPageTable[i].shared;
    NewKernelPageTable[i].physicalPage = KernelPageTable[i].physicalPage;
    NewKernelPageTable[i].valid = KernelPageTable[i].valid;
    NewKernelPageTable[i].backup = KernelPageTable[i].backup;
    NewKernelPageTable[i].use = KernelPageTable[i].use;
    NewKernelPageTable[i].dirty = KernelPageTable[i].dirty;
    NewKernelPageTable[i].readOnly = KernelPageTable[i].readOnly;  	// if the code segment was entirely on
    // a separate page, we could set its
    // pages to be read-only
  }

  for (i = 0; i < numNewPages; i++) {
    NewKernelPageTable[i+numVirtualPages].virtualPage = i + numVirtualPages;
    NewKernelPageTable[i+numVirtualPages].shared = TRUE;
    if (numPagesAllocated == NumPhysPages)
      NewKernelPageTable[i+numVirtualPages].physicalPage = getNextPhysicalPage(i+numVirtualPages, true, -1);
    else
      NewKernelPageTable[i+numVirtualPages].physicalPage = getNextPhysicalPage(i+numVirtualPages, false, -1);

    bzero(&(machine->mainMemory[(NewKernelPageTable[i+numVirtualPages].physicalPage) * PageSize]), PageSize);

    NewKernelPageTable[i+numVirtualPages].valid = TRUE;
    NewKernelPageTable[i+numVirtualPages].backup = true;
    NewKernelPageTable[i+numVirtualPages].use = FALSE;
    NewKernelPageTable[i+numVirtualPages].dirty = FALSE;
    NewKernelPageTable[i+numVirtualPages].readOnly = FALSE;  // if the code segment was entirely on 

    // !--IMPORTANT
    machine->shared[NewKernelPageTable[i].physicalPage] = true;
  }

  KernelPageTable = NewKernelPageTable;


  unsigned int startAddr = numVirtualPages * PageSize;
  numVirtualPages += numNewPages;
  RestoreContextOnSwitch();
  return startAddr;

}

//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------

unsigned
ProcessAddressSpace::getNextPhysicalPage(unsigned int virtualPage, bool replace, int calling_page) {
  stats->pageFaults += 1;
  DEBUG('w', "PageFaults: %d\n", stats->pageFaults);
  int toBeReplaced = -1;
  unsigned long int min_till_now = 4294967295;
  int *cand;
  int i;
  DEBUG('w', "callin_page: %d\n", calling_page);
  if (replace && pageReplacementAlgo != 0) {
    // Implementing Page Replacement Algorithms here
    if (pageReplacementAlgo == RANDOM) {
      //---------------------------------------------------------------

      DEBUG('v', "Searching random page\n");
      toBeReplaced = Random() % NumPhysPages;
      // iterate forever till wanted page is found
      while (machine->shared[toBeReplaced] || toBeReplaced == calling_page) {
        toBeReplaced = Random() % NumPhysPages;
      }
      DEBUG('v', "Called by: [%d]\n", calling_page);
      DEBUG('v', "random page found\n");

      //---------------------------------------------------------------
    } else if (pageReplacementAlgo == FIFO) {
      //---------------------------------------------------------------

      DEBUG('w', "Algo2: %d\n", pageReplacementAlgo);
      int* tmp;
      cand = (int*)PageQueue->Remove();
      toBeReplaced = *cand;
      while(toBeReplaced == calling_page || machine->shared[toBeReplaced]) {
        DEBUG('w', "Algo2: %d\n", pageReplacementAlgo);
        if (toBeReplaced == calling_page) {
          tmp = cand;
          cand = (int*)PageQueue->Remove();
          toBeReplaced = *cand;
        }
        if (machine->shared[toBeReplaced]){
          delete cand;
          cand = (int*)PageQueue->Remove();
          toBeReplaced = *cand;
        }
      }
      if (tmp)
        PageQueue->Append((void*)tmp);
      PageQueue->Append((void*)cand);
      DEBUG('v', "FIFO page found\n");

      //---------------------------------------------------------------
    } else if (pageReplacementAlgo == LRU) {
      //---------------------------------------------------------------

      DEBUG('w', "Algo3: %d\n", pageReplacementAlgo);
      // iterate through the timestamps of all pages and replace the
      // one with leasttime stamp
      for (i = 0; i < NumPhysPages; i++) {
        if (i != calling_page && machine->TimeStamp[i] < min_till_now && !machine->shared[i]) {
          toBeReplaced = i;
          min_till_now = machine->TimeStamp[i];
        }
      }
      // mark the calling page's timestamp
      // just less than the child's timestamp so that
      // child is MRU and parent is 2nd MRU
      if (calling_page != -1) {
        machine->TimeStamp[calling_page] = stats->totalTicks - 1;
      }
      machine->TimeStamp[toBeReplaced] = stats->totalTicks;
      DEBUG('v', "LRU page found\n.");

      //---------------------------------------------------------------
    } else if (pageReplacementAlgo == LRU_CLOCK) {
      //---------------------------------------------------------------

      DEBUG('w', "Algo4: %d\n", pageReplacementAlgo);
      // keep iterating until a page with reference bit reset is found
      while (calling_page == page_pointer || machine->ReferenceBitSet[page_pointer] || 
          machine->shared[page_pointer]) {
        machine->ReferenceBitSet[page_pointer] = false;
        page_pointer += 1;
        // avoid overflow
        page_pointer %= NumPhysPages;
      }
      toBeReplaced = page_pointer;
      machine->ReferenceBitSet[toBeReplaced] = true;
      page_pointer = (page_pointer + 1) % NumPhysPages;
      DEBUG('v', "LRU-CLOCK page found\n.");

      //---------------------------------------------------------------
    }

    // back up the previous page contents into the backup_array if changed
    int pid = machine->PIDatPhysAddr[toBeReplaced];
    if (pid != -1) {
      threadArray[pid]->space->takeBackup(machine->PhysToVirtual[toBeReplaced]);
    }
  } else {
    // there exists a fresh physical page
    numPagesAllocated += 1;
    for (i = 0; i < NumPhysPages; i++) {
      if (machine->PIDatPhysAddr[i] == -1) {
        // a page is found
        toBeReplaced = i;
        if (pageReplacementAlgo == LRU_CLOCK) {
          machine->ReferenceBitSet[i] = true;
        } else if (pageReplacementAlgo == FIFO) {
          DEBUG('w', "replaced with: %d\n", toBeReplaced);
          int* cand = new int;
          *cand = toBeReplaced;
          PageQueue->Append((void*)cand);
        } else if (pageReplacementAlgo == LRU) {
          machine->TimeStamp[toBeReplaced] = stats->totalTicks;
        }
        break;
      }
    }
  }
  machine->PIDatPhysAddr[toBeReplaced] = calling_PID;
  machine->PhysToVirtual[toBeReplaced] = virtualPage;
  return toBeReplaced;
}

void ProcessAddressSpace::takeBackup(int vpn) {
  unsigned tmp;
  if (KernelPageTable[vpn].dirty) {
    tmp = KernelPageTable[vpn].physicalPage;
    memcpy(&(backup_array[vpn * PageSize]), &(machine->mainMemory[tmp * PageSize]), PageSize);
    // reset the dirty bit
    KernelPageTable[vpn].dirty = false;
  }
  KernelPageTable[vpn].valid = false;
  KernelPageTable[vpn].physicalPage = -1;
}


void
ProcessAddressSpace::fixPageFault(unsigned int vadd) {
  unsigned vpn = vadd / PageSize;
  unsigned newPhysicalPage;
  DEBUG('v', "---------------fixing-------\n");
  if (numPagesAllocated == NumPhysPages) {
    newPhysicalPage = getNextPhysicalPage(vpn, true, -1);
  } else {
    newPhysicalPage = getNextPhysicalPage(vpn, false, -1);
  }
  DEBUG('v', "---------------fixed-------\n");
  DEBUG('s', "[VPN: %d], [Allocated: %d]\n", vpn, newPhysicalPage);
  bzero(&(machine->mainMemory[newPhysicalPage * PageSize]), PageSize);
  OpenFile *executable = fileSystem->Open(fileName);

  KernelPageTable[vpn].physicalPage = newPhysicalPage;
  KernelPageTable[vpn].valid = true;


  if (KernelPageTable[vpn].backup) {
    // copy page from backup array to the main memory
    memcpy(&(machine->mainMemory[PageSize * newPhysicalPage]), &(backup_array[PageSize * vpn]), PageSize);
  } else {
    // read directly from executable
    KernelPageTable[vpn].dirty = true;
    executable->ReadAt(&(machine->mainMemory[newPhysicalPage * PageSize]),
        PageSize, noffH.code.inFileAddr + vpn*PageSize);
  }

  KernelPageTable[vpn].backup = true;
  delete executable;
  DEBUG('v', "---------------Going to sleep-------\n");
  currentThread->SortedInsertInWaitQueue(1000+stats->totalTicks);
  DEBUG('v', "---------------Returned from sleep[pid: %d]-------\n\n", calling_PID);
  return;
}
//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------
