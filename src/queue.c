#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>
#include "queue.h"

//check
/* Exercise 1 (Make the queue thread-safe) The implementation of the queue is not threadsafe. 
Two threads working on the queue at the same time could make it fail. Change the queue so
that concurrent accesses work correctly. Check your solution by writing a small testing prototype.
The queue has a limited size, which is specified when it is created. Threads should wait if they
try to remove an element from an empty queue, or if they try to add an element to a full queue.*/

// circular array
typedef struct _queue {
    int size;
    int used;
    int first;
    void **data;
	pthread_cond_t *full;
	pthread_cond_t *empty;
	pthread_mutex_t *mutex;
} _queue;


queue q_create(int size) {
    queue q = malloc(sizeof(_queue));
	
    pthread_mutex_t *mutex;
    if((mutex = malloc(sizeof(pthread_mutex_t))) == NULL){
		printf("Out of memory (No memory for mutex\n");
		exit(1);
	}
	
	pthread_cond_t *full;
	if((full = malloc(sizeof(pthread_cond_t))) == NULL){
		printf("Out of memory (No memory for cond\n");
		exit(1);
	}
	
	pthread_cond_t *empty;
	if((empty = malloc(sizeof(pthread_cond_t))) == NULL){
		printf("Out of memory (No memory for cond\n");
		exit(1);
	}
	
	q->mutex=mutex;
	q->full=full;
	q->empty=empty;

	pthread_cond_init(q->full, NULL);
	pthread_cond_init(q->empty, NULL);
	pthread_mutex_init(q->mutex, NULL);

    q->size  = size;
    q->used  = 0;
    q->first = 0;
    q->data = malloc(size*sizeof(void *));
    
    return q;
}

int q_elements(queue q) {
    return q->used;
}

int q_insert(queue q, void *elem) {
	pthread_mutex_lock(q->mutex);

	while(q->size==q->used){
		pthread_cond_wait(q->full, q->mutex);
	}

   	q->data[(q->first+q->used) % q->size] = elem;    
    q->used++;

	if(q->used==1) {
		pthread_cond_broadcast(q->empty);
	}

	pthread_mutex_unlock(q->mutex);
    
    return 1;
}

void *q_remove(queue q) {
    void *res;

	pthread_mutex_lock(q->mutex);

	while(q->used==0) {
		pthread_cond_wait(q->empty, q->mutex);
	}

	res=q->data[q->first];

	q->first=(q->first+1) % q->size;
	q->used--;

	if(q->used==((q->size)-1)){
		pthread_cond_broadcast(q->full);
	}

	pthread_mutex_unlock(q->mutex);

	return res;
}

void q_destroy(queue q) {
	pthread_cond_destroy(q->full);
	pthread_cond_destroy(q->empty);

	pthread_mutex_destroy(q->mutex);

    free(q->data);
    free(q);
 
}