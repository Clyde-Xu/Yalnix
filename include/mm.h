#ifndef MM_H
#define MM_H
#include "../include/hardware.h"

int CheckPageFrame(int count);
int AllocPageFrame(struct pte *pageTable, int startPage, int count, int prot);
void DeallocPageFrame(struct pte *pageTable, int startPage, int count);
void DuplicateKernelStack(struct pte *target);
void DuplicateUserAll(struct pte *target);

#endif
