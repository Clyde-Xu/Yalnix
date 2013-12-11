#include "../include/hardware.h"
#include "../include/int_handler.h"
#include "../include/IPC.h"
#include "../include/mm.h"
#include "../include/PCB.h"
#include "../include/queue.h"
#include "../include/tty.h"
#include "../include/yalnix.h"

extern PCB *curProc;
extern PCB *idle;
extern Queue readyQueue;
extern Queue clockQueue;
extern Queue revQueue[NUM_TERMINALS];
extern Queue revBlkQueue[NUM_TERMINALS];
extern Queue transBlkQueue[NUM_TERMINALS];
extern int transReady[NUM_TERMINALS];

static char ttyReceive[TERMINAL_MAX_LINE];
static char ttyTransmit[NUM_TERMINALS][TERMINAL_MAX_LINE];
static int ValidatePtr(void *ptr, int length, int prot);
static int ValidateCStyle(void *pointer, int type);
static int SwitchContext(UserContext *uctxt, Queue *queue);
static void Die(int);

// Trap Handlers
void trap_kernel_handler(UserContext *uctxt)
{
	int result;
	int retVal;
	int count;

	// Used by BRK
	void *addr;
	int startPage;

	// Used by DELAY
	int clockticks;

	// Used by FORK
	PCB *child;
	int page;
	int totalPage;
	Entry *entry;

	// Used by EXEC
	char *fileName;
	char **argv;
	int i;

	// Used by WAIT
	int *status_ptr;

	// Used by TTYREAD TTYWRITE PIPEREAD and PIPEWRITE
	int tty_id, pipe_id;
	void *buf;
	int len, total;
	Block *block;

	// Used by IPC
	int *ipc_id;
	int lock_id;
	int cvar_id;
	
	switch(uctxt->code){
		case YALNIX_GETPID:
			retVal = curProc->pid;
			break;
		case YALNIX_BRK:
			addr = (void *)UP_TO_PAGE(uctxt->regs[0]);
			TracePrintf(0, "Brk: Current Addr = %p, Current Brk = %p\n", addr, curProc->brkR1);
			if(addr <= curProc->dataR1){
				TracePrintf(0, "Brk : Trying to Access Text Addr\n");
				retVal = ERROR;
				break;
			}else if((addr + PAGESIZE) > curProc->stackR1){
				TracePrintf(0, "Brk: Trying to Access Stack(Red Zone) Addr\n");
				retVal = ERROR;
				break;
			}else{
				// dataR1 < addr < stackR1
				if(addr >= curProc->brkR1){
					startPage = (int)(curProc->brkR1 - VMEM_1_BASE) >> PAGESHIFT;
					count = (int)(addr - curProc->brkR1) >> PAGESHIFT;
					result = AllocPageFrame(curProc->pageTableR1, startPage, count, PROT_READ | PROT_WRITE);
					if(result == -1){
						TracePrintf(0, "Brk: No Enough Physical Memory\n");
						retVal = ERROR;
						break;
					}
				}else{
					startPage = (int)(addr - VMEM_1_BASE) >> PAGESHIFT;
					count = (int)(curProc->brkR1 - addr) >> PAGESHIFT;
					DeallocPageFrame(curProc->pageTableR1, startPage, count);
				}
				curProc->brkR1 = addr;
				retVal = 0;
			}
			break;
		case YALNIX_DELAY:
			clockticks = uctxt->regs[0];
			if(clockticks < 0){
				retVal = ERROR;
				break;
			}
			else if(clockticks == 0){
				retVal = 0;
			}else{
				curProc->clockticks = clockticks;
				result = SwitchContext(uctxt, &clockQueue);
				if(result == -1){
					TracePrintf(0, "DELAY: No Enough Physical Memory\n");
					retVal = ERROR;
					break;
				}else{
					retVal = 0;
				}
			}
			break;
		case YALNIX_FORK:
			child = createPCB(uctxt);
			if(child == NULL){
				retVal = ERROR;
				break;
			}
			else{
				child->parent = curProc;
				child->dataR1 = curProc->dataR1;
				child->stackR1 = curProc->stackR1;
				child->brkR1 = curProc->brkR1;
				// Reserve Memory for Two Push
				entry = (Entry *)malloc(2 * sizeof(Entry));
				if(entry == NULL){
					TracePrintf(0, "FORK: No Enough Physical Memory\n");
					deallocPCB(child);
					free(child);
					retVal = ERROR;
					break;
				}
				totalPage = ((VMEM_1_LIMIT - (int)child->stackR1) >> PAGESHIFT) + (((int)child->brkR1 - VMEM_1_BASE) >> PAGESHIFT);
				result = CheckPageFrame(totalPage);
				free(entry);
				if(result == -1){
					TracePrintf(0, "FORK: No Enough Physical Memory\n");
					deallocPCB(child);
					free(child);
					retVal = ERROR;
					break;
				}else{
					memcpy(child->pageTableR1, curProc->pageTableR1, sizeof(curProc->pageTableR1));
					page = 0;
					for(; page < VMEM_1_PNUM; page++){
						if(child->pageTableR1[page].valid == 1){
							AllocPageFrame(child->pageTableR1, page, 1, child->pageTableR1[page].prot);
						}
					}
					DuplicateUserAll(child->pageTableR1); 
					push(&curProc->children, child);
					push(&readyQueue, child);
					result = KernelContextSwitch(MyKCS, child, child); 
					if(result != 0){
						TracePrintf(0, "KernelContextSwitch: Error!!\n");
						exit(1);
					}
					if(curProc->state == READY)
						retVal = child->pid;
					else{
						curProc->state = READY;
						retVal = 0;
					}
				}
			}
			break;
		case YALNIX_EXEC:
			fileName = (char *)uctxt->regs[0];
			argv = (char **)uctxt->regs[1];
			result = ValidateCStyle(fileName, sizeof(char));
			if(result == -1){
				retVal = ERROR;
				break;
			}
			result = ValidateCStyle(argv, sizeof(char *));
			if(result == -1){
				retVal = ERROR;
				break;
			}
			i = 0;
			while(argv[i] != NULL){
				result = ValidateCStyle(argv[i++], sizeof(char));
				if(result == -1){
					retVal = ERROR;
					break;
				}
			}
			retVal = LoadProgram(fileName, argv, curProc);
			if(retVal == 0)
				memcpy(uctxt, &curProc->uctxt, sizeof(curProc->uctxt));
			else if(retVal == KILL)
			{
				TracePrintf(0, "EXEC: Stop Current Proc\n");
				Die(KILL);
			}
			break;
		case YALNIX_EXIT:
			TracePrintf(3, "EXIT: Process %d Exit\n", curProc->pid);
			Die(uctxt->regs[0]);
			break;
		case YALNIX_WAIT:
			status_ptr = (int *)uctxt->regs[0];
			result = ValidatePtr(status_ptr, sizeof(int), PROT_READ | PROT_WRITE);
			if(result == -1){
				TracePrintf(0, "WAIT: Invalid ptr = %p\n", status_ptr);
				retVal = ERROR;
				break;
			}
			if(curProc->deadChildren.head != NULL){
			}else if(curProc->children.head != NULL){
				curProc->state = WAIT;
				SwitchContext(uctxt, NULL);
			}else{
				retVal = ERROR;
				break;
			}
			child = pop(&curProc->deadChildren);
			*status_ptr = child->exitStatus;
			retVal = child->pid;
			free(child);
			break;
		case YALNIX_TTY_READ:
			tty_id = (int)uctxt->regs[0];
			if(tty_id < 0 || tty_id >= NUM_TERMINALS){
				retVal = ERROR;
				break;
			}
			buf = (void *)uctxt->regs[1];
			len = (int)uctxt->regs[2];
			result = ValidatePtr(buf, len, PROT_READ | PROT_WRITE);
			if(result == -1){
				retVal = ERROR;
				break;
			}
			while((entry = revQueue[tty_id].head) == NULL){
				result = SwitchContext(uctxt, &revBlkQueue[tty_id]);
				if(result == -1){
					uctxt->regs[0] = ERROR;
					return;
				}
			}
			block = (Block *)entry->content;
			if(len < block->count){
				memcpy(buf, block->ptr, len);
				block->ptr += len;
				block->count -= len;
				retVal = len;
			}else{
				memcpy(buf, block->ptr, block->count);
				if(block->buf != NULL)
					free(block->buf);
				pop(&revQueue[tty_id]);
				retVal = block->count;
			}
			break;
		case YALNIX_TTY_WRITE:
			tty_id = (int)uctxt->regs[0];
			if(tty_id < 0 || tty_id >= NUM_TERMINALS){
				retVal = ERROR;
				break;
			}
			buf = (void *)uctxt->regs[1];
			total = len = uctxt->regs[2];
			result = ValidatePtr(buf, len, PROT_READ);
			if(result == -1){
				retVal = ERROR;
				break;
			}
			while(len > 0){
				while(transReady[tty_id] == 0){
					result = SwitchContext(uctxt, &transBlkQueue[tty_id]);
					if(result == -1)
						continue;
				}
				if(len > TERMINAL_MAX_LINE)
					count = TERMINAL_MAX_LINE;
				else
					count = len;
				memcpy(&ttyTransmit[tty_id], buf, count);
				transReady[tty_id] = 0;
				TtyTransmit(tty_id, &ttyTransmit[tty_id], count);
				buf += TERMINAL_MAX_LINE;
				len -= TERMINAL_MAX_LINE;
				SwitchContext(uctxt, &transBlkQueue[tty_id]);
			}
			retVal = total;
			break;
		case YALNIX_PIPE_INIT:
		case YALNIX_LOCK_INIT:
		case YALNIX_CVAR_INIT:
			ipc_id = (int *)uctxt->regs[0];
			result = ValidatePtr(ipc_id, sizeof(int), PROT_READ | PROT_WRITE);
			if(result == IPC_ERROR){
				TracePrintf(0, "IPC_INIT: Invalid Ptr %p\n", ipc_id);
				retVal = ERROR;
				break;
			}
			if(uctxt->code == YALNIX_PIPE_INIT)
				result = KernelPipeInit(ipc_id);
			else if(uctxt->code == YALNIX_LOCK_INIT)
				result = KernelLockInit(ipc_id);
			else if(uctxt->code == YALNIX_CVAR_INIT)
				result = KernelCvarInit(ipc_id);
			else
				TracePrintf(0, "IPC_INIT: Invalid Type %d\n", uctxt->code);
			if(result == IPC_ERROR)
				retVal = ERROR;
			else
				retVal = 0;
			break;
		case YALNIX_PIPE_READ:
			pipe_id = uctxt->regs[0];
			buf = (void *)uctxt->regs[1];
			total = len = uctxt->regs[2];
			result = ValidatePtr(buf, len, PROT_READ | PROT_WRITE);
			if(result == -1){
				TracePrintf(0, "PIPE_READ: Invalid Ptr %p\n", buf);
				retVal = ERROR;
				break;
			}
			while(1){
				len = KernelPipeRead(pipe_id, buf, total);
				if(len == IPC_ERROR){
					retVal = ERROR;
					break;
				}else if(len == total){
					retVal = uctxt->regs[2];
					break;
				}else{
					total -= len;
					buf += len;
					SwitchContext(uctxt, NULL);
				}
			}
			break;
		case YALNIX_PIPE_WRITE:
			pipe_id = uctxt->regs[0];
			buf = (void *)uctxt->regs[1];
			total = len = uctxt->regs[2];
			result = ValidatePtr(buf, len, PROT_READ);
			if(result == -1){
				TracePrintf(0, "PIPE_WRITE: Invalid Ptr %p\n", buf);
				retVal = ERROR;
				break;
			}
			while(1){
				len = KernelPipeWrite(pipe_id, buf, total);
				if(len == IPC_ERROR){
					retVal = ERROR;
					break;
				}else if(len == total){
					retVal = uctxt->regs[2];
					break;
				}else{
					total -= len;
					buf += len;
					SwitchContext(uctxt, NULL);
				}
			}
			break;
		case YALNIX_LOCK_ACQUIRE:
		case YALNIX_CVAR_WAIT:
			if(uctxt->code == YALNIX_CVAR_WAIT){
				cvar_id = uctxt->regs[0];
				lock_id = uctxt->regs[1];
				result = KernelWait(cvar_id, lock_id);
				if(result == IPC_ERROR){
					retVal = ERROR;
					break;
				}else
					SwitchContext(uctxt, NULL);
			}else
				lock_id = uctxt->regs[0];
			while((result = KernelAcquire(lock_id)) == IPC_BLOCK){
				SwitchContext(uctxt, NULL);
			}
			if(result == IPC_ERROR)
				retVal = ERROR;
			else
				retVal = 0;
			break;
		case YALNIX_LOCK_RELEASE:
			result = KernelRelease(uctxt->regs[0]);
			if(result == IPC_ERROR)
				retVal = ERROR;
			else
				retVal = 0;
			break;
		case YALNIX_CVAR_SIGNAL:
		case YALNIX_CVAR_BROADCAST:
			result = KernelCvarNotify(uctxt->regs[0], uctxt->code);
			if(result == IPC_ERROR)
				retVal = ERROR;
			else
				retVal = 0;	
			break;
		case YALNIX_RECLAIM:
			KernelReclaim(uctxt->regs[0]);
			break;
		default:
			TracePrintf(0, "Kernel Handler: Unspecified System Call\n");
	}
	uctxt->regs[0] = retVal;
}


static void Die(int exitStatus)
{
	if(curProc->pid == 2)
		Halt();
	deallocPCB(curProc);
	// Notify Children
	PCB *child;
	foreach(entry, &curProc->children){
		child = (PCB *)entry->content;
		child->parent = NULL;
	}
	PCB *parent = curProc->parent;
	if(parent != NULL){
		curProc->exitStatus = exitStatus;
		remove(&parent->children, curProc);
		push(&parent->deadChildren, curProc);
		if(parent->state == WAIT){
			parent->state = READY;
			push(&readyQueue, parent);
		}
	}else
		free(curProc);
	curProc = NULL;
	SwitchContext(NULL, NULL);
}


static int ValidatePtr(void *ptr, int length, int prot)
{
	if(ptr == NULL || length < 0){
		TracePrintf(0, "ValidatePtr: NULL PTR or Negative Length\n");
		return -1;
	}
	if((int)ptr < VMEM_1_BASE){
		TracePrintf(0, "ValidatePtr: PTR in Kernel Space\n");
		return -1;
	}else if((int)ptr >= VMEM_1_LIMIT){
		TracePrintf(0, "ValidatePtr: PTR over User Space\n");
		return -1;
	}
	int startPage = (int)(ptr - VMEM_1_BASE) >> PAGESHIFT;
	int endPage = (int)(ptr - VMEM_1_BASE + length - 1) >> PAGESHIFT;
	TracePrintf(3, "Validating Ptr %p with Length %d\n", ptr, length);
	int page = startPage;
	for(; page <= endPage; page++){
		if(curProc->pageTableR1[page].valid == 0 || (curProc->pageTableR1[page].prot & prot) != prot){
			TracePrintf(0, "ValidatePtr: C-style String in Invalid Page\n");
			return -1;
		}
	}
	return 0;
}


static int ValidateCStyle(void *ptr, int type)
{
	while(1){
		int result = ValidatePtr(ptr, type, PROT_READ);
		if(result == -1)
			return -1;
		if(type == sizeof(char)){
			if(*((char *)ptr) == '\0')
				return 0;
			ptr = ((char *)ptr) + 1;
		}else if(type == sizeof(char *)){
			if(*((char **)ptr) == NULL)
				return 0;
			ptr = ((char **)ptr) + 1;
		}
	}
}


void trap_clock_handler(UserContext *uctxt)
{
	// Reduce Clockticks of All Processes in Clcck Queue
	Entry *curEntry = clockQueue.head;
	while(curEntry != NULL){
		PCB *pcb = (PCB *)curEntry->content;
		curEntry = curEntry->next;
		if(--pcb->clockticks == 0){
			remove(&clockQueue, pcb);
			push(&readyQueue, pcb);
		}
	}
	SwitchContext(uctxt, &readyQueue);
}


static int SwitchContext(UserContext *uctxt, Queue *queue)
{
	// If Current Proc is Ready, Push to Ready Queue
	// If Current Proc is Blocked due to Delay, Push to Clock Queue
	// If Current Proc is Blocked due to TTY_READ, Push to RevBlk Queue
	// If Current Proc is Blocked due to TTY_WRITE, Push to TransBlk Queue
	if(queue != NULL && curProc != idle){
		int result = push(queue, curProc);
		if(result == -1)
			return -1;
	}
	PCB *cur_Proc = curProc;
	PCB *next_Proc = pop(&readyQueue);
	// When Current Proc is Dead
	if(cur_Proc != NULL)
		TracePrintf(3, "Cur Pid=%d, Cur sp=%p, Next sp=%p\n", cur_Proc->pid, cur_Proc->uctxt.sp, uctxt->sp);
	if(next_Proc == NULL)
		next_Proc = idle;
	if(cur_Proc != next_Proc){
		curProc = next_Proc;
		if(cur_Proc != NULL)
			memcpy(&cur_Proc->uctxt, uctxt, sizeof(UserContext));
		int result = KernelContextSwitch(MyKCS, cur_Proc, next_Proc);
		if(result != 0){
			TracePrintf(0, "KernelContextSwitch: Error!!\n");
			exit(1);
		}
		memcpy(uctxt, &cur_Proc->uctxt, sizeof(UserContext));
	}
	return 0;
}


void trap_illegal_handler(UserContext *uctxt)
{
	TracePrintf(0, "ILLEGAL TRAP: Proc %d\n", curProc->pid);
	Die(KILL);
}


void trap_memory_handler(UserContext *uctxt)
{
	if(uctxt->code == YALNIX_MAPERR){
		void *addr = (void *)DOWN_TO_PAGE(uctxt->addr);
		if(curProc->brkR1 < addr && addr < curProc->stackR1){
			int startPage = (int)(addr - VMEM_1_BASE) >> PAGESHIFT;
			int count = (curProc->stackR1 - addr) / PAGESIZE;
			int result = AllocPageFrame(curProc->pageTableR1, startPage, count, PROT_READ | PROT_WRITE);
			if(result == -1){
				TracePrintf(0, "MEMORY TRAP: No Enough Memory Proc %d, Addr %p", curProc->pid, uctxt->addr);
				Die(KILL);
			}
			curProc->stackR1 = addr;
		}else{
			TracePrintf(0, "MEMORY TRAP: Invalid Access Proc %d, Addr %p\n", curProc->pid, uctxt->addr);
			Die(KILL);
		}
	}else
		Die(KILL);
}


void trap_math_handler(UserContext *uctxt)
{
	TracePrintf(0, "MATH TRAP: Proc %d\n", curProc->pid);
	Die(KILL);
}


void trap_tty_rev_handler(UserContext *uctxt)
{
	int tty_id = uctxt->code;
	int count = TtyReceive(tty_id, ttyReceive, TERMINAL_MAX_LINE);
	Block *block = (Block *)malloc(sizeof(Block));
	if(block != NULL){
		char *buf;
		if(count == 0)
			buf = NULL;
		else if(count > 0){
			TracePrintf(0, "count = %d\n", count);
			buf = (char *)malloc(count);
			if(buf == NULL){
				free(block);
				return;
			}
			memcpy(buf, ttyReceive, count);
		}
		block->count = count;
		block->buf = buf;
		block->ptr = buf;
		int result = push(&revQueue[tty_id], block);
		if(result == -1){
			if(buf != NULL)
				free(buf);
			free(block);
			return;
		}
		PCB *pcb;
		while((pcb = pop(&revBlkQueue[tty_id])) != NULL)
			push(&readyQueue, pcb);
	}
}


void trap_tty_trans_handler(UserContext *uctxt)
{
	int tty_id = uctxt->code;
	transReady[tty_id] = 1;
	PCB *pcb;
	while((pcb = pop(&transBlkQueue[tty_id])) != NULL)
		push(&readyQueue, pcb);
}


void trap_dummy_handler(UserContext *uctxt)
{
	TracePrintf(0, "DUMMY TRAP: Unspecified Trap %d\n", uctxt->vector);
}
