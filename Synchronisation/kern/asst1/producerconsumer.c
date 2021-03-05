/* This file will contain your solution. Modify it as you wish. */
#include <types.h>
#include <lib.h>
#include <synch.h>
#include "producerconsumer_driver.h"

/* Declare any variables you need here to keep track of and
   synchronise your bounded. A sample declaration of a buffer is shown
   below. It is an array of pointers to items.

   You can change this if you choose another implementation.
   However, you should not have a buffer bigger than BUFFER_SIZE
*/

typedef struct QNode {
    data_item_t *value;
    struct QNode *next;
} QNode;

typedef struct Queue {
    QNode *front, *end;
} Queue;

Queue *item_buffer;

struct semaphore *mutex;
struct semaphore *empty;
struct semaphore *full;


/* consumer_receive() is called by a consumer to request more data. It
   should block on a sync primitive if no data is available in your
   buffer. It should not busy wait! */
data_item_t *consumer_receive(void)
{
        data_item_t *item;

        P(full);
        P(mutex);

        // Essentially just the Dequeue method from a Queue ADT
        QNode *temp = item_buffer->front;
        item_buffer->front = item_buffer->front->next;
        if (item_buffer->front == NULL) item_buffer->end = NULL;

        item = temp->value;
        kfree(temp);

        V(mutex);
        V(empty);

        return item;
}

/* procucer_send() is called by a producer to store data in your
   bounded buffer.  It should block on a sync primitive if no space is
   available in your buffer. It should not busy wait!*/
void producer_send(data_item_t *item)
{
        QNode *newNode = (QNode *)kmalloc(sizeof(QNode));
        newNode->value = item;
        newNode->next = NULL;

        P(empty);
        P(mutex);

        // Essentially the Enqueue method from a Queue ADT
        if (item_buffer->end == NULL) item_buffer->front = item_buffer->end = newNode;
        else {
            item_buffer->end->next = newNode;
            item_buffer->end = newNode;
        }

        V(mutex);
        V(full);
}

/* Perform any initialisation (e.g. of global data) you need
   here. Note: You can panic if any allocation fails during setup */
void producerconsumer_startup(void) {
    mutex = sem_create("mutex", 1);
    full = sem_create("full", 0);
    empty = sem_create("empty", BUFFER_SIZE);

    item_buffer = (Queue *) kmalloc(sizeof(Queue));
    item_buffer->front = item_buffer->end = NULL;
}

/* Perform any clean-up you need here */
void producerconsumer_shutdown(void) {
    sem_destroy(mutex);
    sem_destroy(full);
    sem_destroy(empty);

    kfree(item_buffer);
}
