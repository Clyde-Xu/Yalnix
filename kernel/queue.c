#include "../include/hardware.h"
#include "../include/queue.h"


int push(Queue *queue, void *content)
{
	Entry *entry = (Entry *)malloc(sizeof(Entry));
	if(entry == NULL){
		TracePrintf(0, "push: Not Enough Memory\n");
		return -1;
	}
	entry->content = content;
	entry->next = NULL;
	entry->prev = queue->tail;
	if(queue->tail != NULL)
		queue->tail->next = entry;
	else
		queue->head = entry;
	queue->tail = entry;
	return 0;
}


void *pop(Queue *queue)
{
	void *content = NULL;
	if(queue->head != NULL){
		content = queue->head->content;
		Entry *entry = queue->head;
		queue->head = queue->head->next;
		free(entry);
		if(queue->head == NULL)
			queue->tail = NULL;
		else
			queue->head->prev = NULL;
	}
	return content;
}


void remove(Queue *queue, void *content)
{
	foreach(entry, queue){
		if(entry->content == content){
			if(entry->prev != NULL)
				entry->prev->next = entry->next;
			else
				queue->head = entry->next;
			if(entry->next != NULL)
				entry->next->prev = entry->prev;
			else
				queue->tail = entry->prev;
			free(entry);
			break;
		}
	}
}
