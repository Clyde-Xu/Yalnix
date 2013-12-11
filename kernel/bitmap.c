#include "../include/bitmap.h"

#define EXT_OFFSET(POS) ((POS) / 8)
#define INT_OFFSET(POS) ((POS) % 8)

void Setbit(char *bitmap, int pos)
{
	bitmap[EXT_OFFSET(pos)] |= 1 << INT_OFFSET(pos);
}


void Clearbit(char *bitmap, int pos)
{
	bitmap[EXT_OFFSET(pos)] &= ~(1 << INT_OFFSET(pos));
}


int Getbit(char *bitmap, int pos)
{
	return bitmap[EXT_OFFSET(pos)] & (1 << INT_OFFSET(pos));
}
