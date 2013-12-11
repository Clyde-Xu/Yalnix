#ifndef QUEUE_H
#define QUEUE_H

#define foreach(entry, queue) \
	Entry *entry = (queue)->head; \
	for(; entry != NULL; entry = entry->next)

typedef struct QEntry{
	void *content;
	struct QEntry *next;
	struct QEntry *prev;
}Entry;

typedef struct{
	Entry *head;
	Entry *tail;
}Queue;

int push(Queue *, void *);
void *pop(Queue *);
void remove(Queue *, void *);

#endif
