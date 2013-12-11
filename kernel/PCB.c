#include "../include/mm.h"
#include "../include/PCB.h"

static pid = 1;

PCB *createPCB(UserContext *uctxt)
{
	PCB *pcb = (PCB *)malloc(sizeof(PCB));
	if(pcb != NULL){
		TracePrintf(0, "createPCB:  Pages\n");
		memcpy(&pcb->uctxt, uctxt, sizeof(UserContext));
		pcb->state = NEW;
		pcb->pid = pid++;
		pcb->children.head = pcb->children.tail = NULL;
		pcb->deadChildren.head = pcb->deadChildren.tail = NULL;
		memset(pcb->pageTableR1, 0, sizeof(pcb->pageTableR1));
		int result = AllocPageFrame(pcb->pageTableStackR0, 0, KERNEL_STACK_PNUM, PROT_READ | PROT_WRITE);
		if(result == -1){
			TracePrintf(0, "createPCB: No Enough Physical Memory for Pages\n");
			free(pcb);
			pcb = NULL;
		}
	}else
		TracePrintf(0, "createPCB: No Enough Physical Memory for PCB\n");
	return pcb;
}


void deallocPCB(PCB *pcb)
{
	DeallocPageFrame(pcb->pageTableR1, 0, VMEM_1_PNUM);
	DeallocPageFrame(pcb->pageTableStackR0, 0, KERNEL_STACK_PNUM);
}
