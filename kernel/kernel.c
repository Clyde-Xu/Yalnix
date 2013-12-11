#include "../include/bitmap.h"
#include "../include/hardware.h"
#include "../include/int_handler.h"
#include "../include/IPC.h"
#include "../include/load_info.h"
#include "../include/mm.h"
#include "../include/PCB.h"
#include "../include/queue.h"

#include <string.h>

void *kernelDataStart;
void *kernelDataEnd;

// Page Table Region 0
static struct pte ptr0[VMEM_0_PNUM];

// Trap Handler
static handler intvec[TRAP_VECTOR_SIZE];
static void DuplicatePageFrame(struct pte *target, void *srcAddr, int count);


PCB *curProc;
PCB *idle;
Queue readyQueue;
Queue clockQueue;
Queue revQueue[NUM_TERMINALS];
Queue revBlkQueue[NUM_TERMINALS];
Queue transBlkQueue[NUM_TERMINALS];
int transReady[NUM_TERMINALS];
static char *bitmap;
static int mark = 0;
static int freeFrameNum;
static int vm_enable = 0;


void KernelStart(char *cmd_args[], unsigned int pmem_size, UserContext *uctxt)
{
	TracePrintf(0, "kernel start\n");

	// Trap Handler
	int i = 0;
	for(; i < TRAP_VECTOR_SIZE; i++)
		intvec[i] = trap_dummy_handler;
	intvec[TRAP_KERNEL] = trap_kernel_handler;
	intvec[TRAP_CLOCK] = trap_clock_handler;
	intvec[TRAP_ILLEGAL] = trap_illegal_handler;
	intvec[TRAP_MEMORY] = trap_memory_handler;
	intvec[TRAP_MATH] = trap_math_handler;
	intvec[TRAP_TTY_RECEIVE] = trap_tty_rev_handler;
	intvec[TRAP_TTY_TRANSMIT] = trap_tty_trans_handler;
	WriteRegister(REG_VECTOR_BASE, (signed int)&intvec); 
	
	// Bitmap for Physical Memory
	freeFrameNum = pmem_size / PAGESIZE;
	TracePrintf(0, "Total 0x%x Pages of Physical Memory\n", freeFrameNum);
	int sizeOfChar = (freeFrameNum + 7) / 8;
	bitmap = (char *)malloc(sizeOfChar);
	bzero(bitmap, sizeOfChar);

	// Initialize Page Table Entry at Boot Time
	int startPage = (int)kernelDataStart >> PAGESHIFT;
	int endPage = (int)kernelDataEnd >> PAGESHIFT;
	int page;
	for(page = 0; page < endPage; page++){
		ptr0[page].valid = 1;
		ptr0[page].pfn = page;
		if(page < startPage)
			ptr0[page].prot = PROT_READ | PROT_EXEC;
		else
			ptr0[page].prot = PROT_READ | PROT_WRITE;
		Setbit(bitmap, page);
		TracePrintf(3, "Kernel Text Page Mapping:%d==>%d\n", page, page);
	}
	freeFrameNum -= endPage;
	TracePrintf(2, "Kernel Stack From 0x%x to 0x%x\n", KERNEL_STACK_BASE, KERNEL_STACK_LIMIT);
	for(page = KERNEL_STACK_BASEPAGE; page < KERNEL_STACK_LIMITPAGE; page++){
		ptr0[page].valid = 1;
		ptr0[page].pfn = page;
		ptr0[page].prot = PROT_READ | PROT_WRITE;
		Setbit(bitmap, page);
		TracePrintf(3, "Kernel Stack Page Mapping:%d==>%d\n", page, page);
		freeFrameNum--;
	}

	// Enable VM
	WriteRegister(REG_PTBR0, (unsigned int)ptr0);
	WriteRegister(REG_PTLR0, (unsigned int)VMEM_0_PNUM);
	WriteRegister(REG_VM_ENABLE, 1);
	vm_enable = 1;

	// Initialize Ready Queue and First Process
	clockQueue.head = clockQueue.tail = NULL;
	readyQueue.head = readyQueue.tail = NULL;
	InitIPC();
	for(i = 0; i < NUM_TERMINALS; i++){
		revBlkQueue[i].head = revBlkQueue[i].tail = NULL;
		revQueue[i].head = revQueue[i].tail = NULL;
		transBlkQueue[i].head = transBlkQueue[i].tail = NULL;
		transReady[i] = 1;
	}
	idle = createPCB(uctxt);
	idle->state = READY;

	char *args[1];
	args[0] = NULL;
	WriteRegister(REG_PTBR1, (unsigned int)idle->pageTableR1);
	WriteRegister(REG_PTLR1, (unsigned int)VMEM_1_PNUM);
	LoadProgram("program/idle", args, idle);
	KernelContextSwitch(MyKCS, idle, idle);
	if(mark == 1){
		TracePrintf(2, "Idle Process Start\n");
		memcpy(uctxt, &idle->uctxt, sizeof(UserContext));
		return;
	}
	mark = 1;
	PCB *init = createPCB(uctxt);
	init->state = READY;
	curProc = init;
	WriteRegister(REG_PTBR1, (unsigned int)init->pageTableR1);
	int argc = 0;
	for(; cmd_args[argc] != NULL; argc++);
	if(argc == 0)
		LoadProgram("program/init", args, init);
	else
		LoadProgram(cmd_args[0], cmd_args, init);
	DeallocPageFrame(init->pageTableStackR0, 0, KERNEL_STACK_PNUM);
	page = 0;
	while(page < (KERNEL_STACK_PNUM)){
		init->pageTableStackR0[page] = ptr0[KERNEL_STACK_BASEPAGE + page];
		page++;
	}
	memcpy(uctxt, &init->uctxt, sizeof(UserContext));
}


//	TEXT PROT_READ | PROT_EXEC
//	DATA Start->End PROT_READ | PROT_END
void SetKernelData(void *_KernelDataStart, void *_KernelDataEnd)
{
	TracePrintf(0, "Start=%p, End=%p\n", _KernelDataStart, _KernelDataEnd);
	kernelDataStart = _KernelDataStart;
	kernelDataEnd = _KernelDataEnd;
}


int AllocPageFrame(struct pte *pageTable, int startPage, int count, int prot)
{
	if(freeFrameNum < count){
		return -1;
	}else{
		int i, pos = 0;
		for(i = startPage; i < startPage + count; i++){
			while(Getbit(bitmap, pos) != 0)
				pos++;
			pageTable[i].valid = 1;
			pageTable[i].pfn = pos;
			pageTable[i].prot = prot;
			Setbit(bitmap, pos++);
			TracePrintf(3, "Mapping: 0x%x==>0x%x\n", i, pos-1);
		}
		freeFrameNum -= count;
		return 0;
	}
}


void DeallocPageFrame(struct pte *pageTable, int startPage, int count)
{
	int page, recycle = 0;
	for(page = startPage; page < startPage + count; page++){
		if(pageTable[page].valid != 0){
			Clearbit(bitmap, pageTable[page].pfn);
			recycle++;
			TracePrintf(3, "UnMapping: 0x%x==>0x%x\n", page, pageTable[page].pfn);
		}
	}
	memset(&pageTable[startPage], 0, count * sizeof(struct pte));
	freeFrameNum += recycle;
}


int CheckPageFrame(count)
{
	/*
	TracePrintf(2, "Current Free Frame = %d\n", freeFrameNum);
	int i=0, c=0;
	for(;i<512;i++){
		if(Getbit(bitmap, i) == 0)
			c++;
	}
	TracePrintf(2, "Current Free Frame = %d\n", c);*/
	if(count <= freeFrameNum)
		return 0;
	else
		return -1;
}


void DuplicateKernelStack(struct pte *target)
{
	DuplicatePageFrame(target, (void *)KERNEL_STACK_BASE, KERNEL_STACK_PNUM);
}


void DuplicateUserAll(struct pte *target)
{
	DuplicatePageFrame(target, (void *)VMEM_1_BASE, VMEM_0_PNUM); 
}


static void DuplicatePageFrame(struct pte *target, void *srcAddr, int count)
{
	TracePrintf(2, "Duplicate from Src = %p, Count = %d\n", srcAddr, count);
	int s_page = KERNEL_STACK_BASEPAGE - 1;
	void *s_addr = (void *)(s_page << PAGESHIFT);
	struct pte s_pte = ptr0[s_page];
	ptr0[s_page].valid = 1;
	ptr0[s_page].prot = PROT_READ | PROT_WRITE;
	int page = 0;
	for(; page < count; srcAddr += PAGESIZE, page++){
		if(target[page].valid == 1){
			ptr0[s_page].pfn = target[page].pfn;
			WriteRegister(REG_TLB_FLUSH, (unsigned int)s_addr);
			memcpy(s_addr, srcAddr, PAGESIZE);
		}
	}
	ptr0[s_page] = s_pte;
	WriteRegister(REG_TLB_FLUSH, (unsigned int)s_addr);
}


// The addr is automatically round to the boundary.
int SetKernelBrk(void *addr)
{
	TracePrintf(0, "SetKernelBrk: Current Addr = %p\n", addr);
	if((int)addr > KERNEL_STACK_BASE){
		TracePrintf(0, "SetKernelBrk: Trying to Access Stack Addr = %p\n", addr);
		return -1;
	}else if(addr <= kernelDataStart){
		TracePrintf(0, "SetKernelBrk: Trying to Access Text Addr = %p\n", addr);
		return -1;
	}else{
		if(!vm_enable)
			kernelDataEnd = addr;
		else{
			int startPage;
			int count;
			if(addr >= kernelDataEnd){
				startPage = (int)kernelDataEnd >> PAGESHIFT;
				count = ((int)addr >> PAGESHIFT) - startPage;
				int result = AllocPageFrame(ptr0, startPage, count, PROT_READ | PROT_WRITE);
				if(result == -1){
					TracePrintf(0, "SetKernelBrk: No Enough Memory\n");
					return -1;
				}
			}else{
				startPage = (int)addr >> PAGESHIFT;
				count = ((int)kernelDataEnd >> PAGESHIFT) - startPage;
				DeallocPageFrame(ptr0, startPage, count);
			}
			kernelDataEnd = addr;
		}
		return 0;
	}
}


KernelContext *MyKCS(KernelContext *kctxt, void *oldPCB, void *newPCB)
{
	PCB *new_PCB = (PCB *)newPCB;
	if(oldPCB == newPCB){
		memcpy(&new_PCB->kctxt, kctxt, sizeof(new_PCB->kctxt));
		DuplicateKernelStack(new_PCB->pageTableStackR0);
	}else{
		if(oldPCB != NULL)
			memcpy(&((PCB *)oldPCB)->kctxt, kctxt, sizeof(KernelContext));
		memcpy(&ptr0[KERNEL_STACK_BASEPAGE], new_PCB->pageTableStackR0, sizeof(new_PCB->pageTableStackR0));
		WriteRegister(REG_PTBR1, (unsigned int)new_PCB->pageTableR1);
		WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1);
		int addr;
		for(addr = KERNEL_STACK_BASE; addr < KERNEL_STACK_LIMIT; addr += PAGESIZE)
			WriteRegister(REG_TLB_FLUSH, addr);
	}
	return &new_PCB->kctxt;
}
