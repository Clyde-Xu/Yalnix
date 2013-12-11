#ifndef IPC_H
#define IPC_H

#include "../include/queue.h"

#define PIPE_LEN	1024
#define IPC_ERROR	-1
#define IPC_BLOCK	-2


typedef enum{
	PIPE,
	LOCK,
	COND,
}Type;

typedef struct{
	int id;
	Type type;
	void *content;
}IPC;

typedef struct{
	char buf[PIPE_LEN];
	char *read_ptr;
	char *write_ptr;
	Queue readQueue;
	Queue writeQueue;
}Pipe;

typedef struct{
	int locking;
	void *lockProc;
	Queue lockQueue;
}Lock;

typedef struct{
	Queue waitQueue;
}Cond;

void InitIPC(void);
int KernelPipeInit(int *);
int KernelPipeRead(int, void *, int);
int KernelPipeWrite(int, void *, int);
int KernelLockInit(int *);
int KernelAcquire(int);
int KernelRelease(int);
int KernelCvarInit(int *);
int KernelCvarNotify(int, int);
int KernelWait(int, int);
void KernelReclaim(int);

#endif
