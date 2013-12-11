#include "../include/hardware.h"
#include "../include/IPC.h"
#include "../include/PCB.h"
#include "../include/queue.h"
#include "../include/yalnix.h"

// Pipe Lock or Condition Variable's Id 
static int id = 0;
Queue ipcQueue;
extern Queue readyQueue;
extern void *curProc;
static IPC *createIPC(Type, void *);
static void destroyIPC(int);

void InitIPC(void)
{
	ipcQueue.head = ipcQueue.tail = NULL;
}


int KernelPipeInit(int *pipe_id)
{
	Pipe *pipe = (Pipe *)malloc(sizeof(Pipe));
	if(pipe == NULL){
		TracePrintf(0, "KernelPipeInit: Pipe Init Failed\n");
		return IPC_ERROR;
	}
	pipe->read_ptr = pipe->write_ptr = pipe->buf;
	pipe->readQueue.head = pipe->readQueue.tail = NULL;
	pipe->writeQueue.head = pipe->writeQueue.tail = NULL;
	IPC *ipc = createIPC(PIPE, pipe);
	if(ipc == NULL){
		free(pipe);
		TracePrintf(0, "KernelPipeInit: Pipe(IPC) Init Failed\n");
		return IPC_ERROR;
	}
	int result = push(&ipcQueue, ipc);
	if(result == -1){
		free(ipc);
		free(pipe);
		TracePrintf(0, "KernelPipeInit: Pipe(Push) Init Failed\n");
		return IPC_ERROR;
	}
	*pipe_id = ipc->id;
	return 0;
}


// Read until satisfied or empty (pipe->read_ptr == pipe->write_ptr)
// Put curProc into pipe->readQueue if not satisfied
int KernelPipeRead(int pipe_id, void *buf, int len)
{
	Pipe *pipe = NULL;
	foreach(entry, &ipcQueue){
		IPC *ipc = entry->content;
		if(ipc->id == pipe_id && ipc->type == PIPE){
			pipe = (Pipe *)ipc->content;
			break;
		}
	}
	if(pipe == NULL){
		TracePrintf(0, "KernelPipeRead: Pipe %d Does Not Exist\n", pipe_id);
		return IPC_ERROR;
	}
	int retVal = 0;
	int over = 0;
	if(pipe->read_ptr > pipe->write_ptr){
		int backLen = PIPE_LEN - (pipe->read_ptr - pipe->buf);
		if(backLen >= len){
			retVal = len;
			memcpy(buf, pipe->read_ptr, len);
			pipe->read_ptr += len;
			if(pipe->read_ptr == (pipe->buf + PIPE_LEN))
				pipe->read_ptr = pipe->buf;
			over = 1;
		}else{
			retVal = backLen;
			memcpy(buf, pipe->read_ptr, backLen);
			buf += backLen;
			len -= backLen;
			pipe->read_ptr = pipe->buf;
		}
	}
	if(!over){
		int frontLen = pipe->write_ptr - pipe->read_ptr;
		if(len > frontLen){
			len = frontLen;
			int result = push(&pipe->readQueue, curProc);
			if(result == -1)
				return IPC_ERROR;
		}
		retVal += len;
		memcpy(buf, pipe->read_ptr, len);
		pipe->read_ptr += len;
	}
	void *pcb;
	while((pcb = pop(&pipe->writeQueue)) != NULL)
		push(&readyQueue, pcb);
	return retVal;
}


// Write until satisfied or empty (pipe->read_ptr == (pipe->write_ptr + 1) or pipe->read_ptr and pipe->write_ptr stand at two ends)
// Put curProc into pipe->writeQueue if not satisfied
int KernelPipeWrite(int pipe_id, void *buf, int len)
{
	Pipe *pipe = NULL;
	foreach(entry, &ipcQueue){
		IPC *ipc = (IPC *)entry->content;
		if(ipc->id == pipe_id && ipc->type == PIPE){
			pipe = (Pipe *)ipc->content;
			break;
		}
	}
	if(pipe == NULL){
		TracePrintf(0, "KernelPipeWrite: Pipe %d Does Not Exist\n", pipe_id);
		return IPC_ERROR;
	}
	int retVal = 0;
	int over = 0;
	if(pipe->write_ptr >= pipe->read_ptr){
		int backLen = PIPE_LEN - (pipe->write_ptr - pipe->buf);
		if(pipe->read_ptr == pipe->buf){
			over = 1;
			backLen--;
		}
		if(backLen >= len){
			retVal = len;
			memcpy(pipe->write_ptr, buf, len);
			pipe->write_ptr += len;
			if(pipe->write_ptr == (pipe->buf + PIPE_LEN))
				pipe->write_ptr = pipe->buf;
			over = 1;
		}else{
			retVal = backLen;
			memcpy(pipe->write_ptr, buf, backLen);
			buf += backLen;
			len -= backLen;
			pipe->write_ptr += backLen;
			if(!over)
				pipe->write_ptr = pipe->buf;
			else{
				int result = push(&pipe->writeQueue, curProc);
				if(result == -1)
					return IPC_ERROR;
			}
		}
	}
	if(!over){
		int frontLen = pipe->read_ptr - pipe->write_ptr - 1;
		if(len > frontLen){
			len = frontLen;
			int result = push(&pipe->writeQueue, curProc);
			if(result == -1)
				return IPC_ERROR;
		}
		retVal += len;
		memcpy(pipe->write_ptr, buf, len);
		pipe->write_ptr += len;
	}
	void *pcb;
	while((pcb = pop(&pipe->readQueue)) != NULL)
		push(&readyQueue, pcb);
	return retVal;
}


int KernelLockInit(int *lock_id)
{
	Lock *lock = (Lock *)malloc(sizeof(Lock));
	if(lock == NULL){
		TracePrintf(0, "KernelLockInit: Lock Init Failed\n");
		return IPC_ERROR;
	}
	lock->locking = 0;
	lock->lockQueue.head = lock->lockQueue.tail = NULL;
	IPC *ipc = createIPC(LOCK, lock);
	if(ipc == NULL){
		free(lock);
		TracePrintf(0, "KernelLockInit: Lock(IPC) Init Failed\n");
		return IPC_ERROR;
	}
	int result = push(&ipcQueue, ipc);
	if(result == -1){
		free(ipc);
		free(lock);
		TracePrintf(0, "KernelLockInit: Lock(Push) Init Failed\n");
		return IPC_ERROR;
	}
	*lock_id = ipc->id;
	return 0;
}


int KernelAcquire(int lock_id)
{
	Lock *lock = NULL;
	foreach(entry, &ipcQueue){
		IPC *ipc = (IPC *)entry->content;
		if(ipc->id == lock_id && ipc->type == LOCK){
			lock = (Lock *)ipc->content;
			break;
		}
	}
	if(lock == NULL){
		TracePrintf(0, "KernelAcquire: Lock %d Does Not Exist\n", lock_id);
		return IPC_ERROR;
	}
	if(lock->locking){
		TracePrintf(0, "KernelAcquire: Lock %d Is Locked By Proc %d\n", lock_id, ((PCB *)lock->lockProc)->pid);
		int result = push(&lock->lockQueue, curProc);
		if(result == -1)
			return IPC_ERROR;
		return IPC_BLOCK;
	}
	TracePrintf(2, "KernelAcquire: Lock Obtained By Proc %d\n", ((PCB *)curProc)->pid);
	lock->locking = 1;
	lock->lockProc = curProc;
	return 0;
}


int KernelRelease(int lock_id)
{
	Lock *lock = NULL;
	foreach(entry, &ipcQueue){
		IPC *ipc = (IPC *)entry->content;
		if(ipc->id == lock_id && ipc->type == LOCK){
			lock = (Lock *)ipc->content;
			break;
		}
	}
	if(lock == NULL){
		TracePrintf(0, "KernelRelease: Lock %d Does Not Exist\n", lock_id);
		return IPC_ERROR;
	}
	if(lock->locking){
		if(lock->lockProc == curProc){
			lock->locking = 0;
			lock->lockProc = NULL;
			void *pcb;
			while((pcb = pop(&lock->lockQueue)) != NULL)
				push(&readyQueue, pcb);
			TracePrintf(2, "KernelRelease: Lock Released By Proc %d\n", ((PCB *)curProc)->pid);
			return 0;
		}else{
			TracePrintf(0, "KernelRelease: Lock %d Is Locked By Proc %d\n", lock_id, ((PCB *)curProc)->pid);
			return IPC_ERROR;
		}
	}else{
		TracePrintf(0, "KernelRelease: Lock %d Isn't Locked\n", lock_id);
		return 0;
	}
}


int KernelCvarInit(int *cvar_id)
{
	Cond *cond = (Cond *)malloc(sizeof(Cond));
	if(cond == NULL){
		TracePrintf(0, "KernelCvarInit: Cond Init Failed\n");
		return IPC_ERROR;
	}
	cond->waitQueue.head = cond->waitQueue.tail = NULL;
	IPC *ipc = createIPC(COND, cond);
	if(ipc == NULL){
		free(cond);
		TracePrintf(0, "KernelCvarInit: Cond(IPC) Init Failed\n");
		return IPC_ERROR;
	}
	int result = push(&ipcQueue, ipc);
	if(result == -1){
		free(cond);
		free(ipc);
		TracePrintf(0, "KernelCvarInit: Cond(Push) Init Failed\n");
		return IPC_ERROR;
	}
	*cvar_id = ipc->id;
	return 0;
}


int KernelCvarNotify(int cvar_id, int type)
{
	Cond *cond = NULL;
	foreach(entry, &ipcQueue){
		IPC *ipc = (IPC *)entry->content;
		if(ipc->id == cvar_id && ipc->type == COND){
			cond = (Cond *)ipc->content;
			break;
		}
	}
	if(cond == NULL){
		TracePrintf(0, "KernelCvarSignal: Cond %d Does Not Exist\n", cvar_id);
		return IPC_ERROR;
	}
	void *pcb = NULL;
	do{
		if((pcb = pop(&cond->waitQueue)) != NULL)
			push(&readyQueue, pcb);
	}while(type == YALNIX_CVAR_BROADCAST && pcb != NULL);
	return 0;
}


int KernelWait(int cvar_id, int lock_id)
{
	Cond *cond = NULL;
	Lock *lock = NULL;
	foreach(entry, &ipcQueue){
		IPC *ipc = (IPC *)entry->content;
		if(ipc->id == cvar_id && ipc->type == COND){
			cond = (Cond *)ipc->content;
			break;
		}
	}
	if(cond == NULL){
		TracePrintf(0, "KernelWait: Either Cvar %d Does Not Exist\n", cvar_id);
		return IPC_ERROR;
	}
	int result = KernelRelease(lock_id); 
	if(result == 0){
		result = push(&cond->waitQueue, curProc);
		if(result == -1)
			return IPC_ERROR;
	}
	return result;
}


void KernelReclaim(int ipc_id)
{
	Pipe *pipe;
	Lock *lock;
	Cond *cond;
	void *pcb;
	foreach(entry, &ipcQueue){
		IPC *ipc = entry->content;
		if(ipc->id == ipc_id){
			switch(ipc->type){
				case PIPE:
					pipe = (Pipe *)ipc->content;
					while((pcb = pop(&pipe->readQueue)) != NULL)
						push(&readyQueue, pcb);
					while((pcb = pop(&pipe->writeQueue)) != NULL)
						push(&readyQueue, pcb);
					break;
				case LOCK:
					TracePrintf(0, "KernelReclaim: Lock %d Reclaimed By Proc %d\n", ipc_id, ((PCB *)curProc)->pid);
					lock = (Lock *)ipc->content;
					while((pcb = pop(&lock->lockQueue)) != NULL)
						push(&readyQueue, pcb);
					break;
				case COND:
					cond = (Cond *)ipc->content;
					while((pcb = pop(&cond->waitQueue)) != NULL)
						push(&readyQueue, pcb);
					break;
				default:
					TracePrintf(0, "KernelReclaim: Undefined IPC Type %d\n", ipc->type);
					break;
			}
			remove(&ipcQueue, ipc);
			free(ipc->content);
			free(ipc);
			break;
		}
	}
}


static IPC *createIPC(Type type, void *content)
{
	IPC *ipc = (IPC *)malloc(sizeof(IPC));
	if(ipc != NULL){
		ipc->id = id++;
		ipc->type = type;
		ipc->content = content;
	}
	return ipc;
}
