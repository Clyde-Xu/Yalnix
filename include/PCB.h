#ifndef PCB_H
#define PCB_H
#include "../include/hardware.h"
#include "../include/queue.h"

enum State{
	NEW,
	READY,
	WAIT,
};

typedef struct _PCB{
	int pid;
	int exitStatus;
	struct _PCB *parent;
	Queue children;
	Queue deadChildren;
	void *dataR1;
	void *brkR1;
	void *stackR1;
	int clockticks;
	enum State state;
	UserContext uctxt;
	KernelContext kctxt;
	struct pte pageTableR1[VMEM_1_PNUM];
	struct pte pageTableStackR0[KERNEL_STACK_PNUM];
}PCB;


PCB *createPCB(UserContext *uctxt);
void deallocPCB(PCB *pcb);

#endif
