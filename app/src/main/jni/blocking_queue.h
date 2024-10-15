
#include <stdlib.h>

typedef struct blocking_queue_node_t {
    void *data;
    struct blocking_queue_node_t *next;
} blocking_queue_node_t;

typedef struct blocking_queue_t {
    blocking_queue_node_t *head, *tail;
    int size;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} blocking_queue_t;

typedef struct blocking_queue_optional_data {
    bool has_data;
    void *data;
} blocking_queue_optional_data;

/**
 * Create an unlimited blocking queue.
 */
blocking_queue_t *blocking_queue_create();

void blocking_queue_destroy(blocking_queue_t *queue);

int blocking_queue_push(blocking_queue_t *queue, void *data);

void *blocking_queue_pop(blocking_queue_t *queue);

int blocking_queue_size(blocking_queue_t *queue);

#ifndef C_BLOCKING_QUEUE
#define C_BLOCKING_QUEUE

blocking_queue_t *blocking_queue_create() {
    blocking_queue_t *queue = (blocking_queue_t *) malloc(sizeof(blocking_queue_t));
    if (queue == NULL) {
        return NULL;
    }
    queue->head = NULL;
    queue->tail = NULL;
    queue->size = 0;
    pthread_mutex_init(&queue->lock, NULL);
    pthread_cond_init(&queue->cond, NULL);
    return queue;
}

void blocking_queue_destroy(blocking_queue_t *queue) {
    pthread_mutex_lock(&queue->lock);
    while (queue->head != NULL) {
        blocking_queue_node_t *temp = queue->head;
        queue->head = queue->head->next;
        free(temp);
    }
    pthread_mutex_unlock(&queue->lock);
    pthread_mutex_destroy(&queue->lock);
    pthread_cond_destroy(&queue->cond);
    free(queue);
}

int blocking_queue_size(blocking_queue_t *queue) {
    pthread_mutex_lock(&queue->lock);
    int sz = queue->size;
    pthread_mutex_unlock(&queue->lock);
    return sz;
}

int blocking_queue_push(blocking_queue_t *queue, void *data) {
    pthread_mutex_lock(&queue->lock);

    blocking_queue_node_t *node = (blocking_queue_node_t *) malloc(sizeof(blocking_queue_node_t));
    if (node == NULL) {
        pthread_mutex_unlock(&queue->lock);
        return -1;
    }

    node->data = data;
    node->next = NULL;

    if (queue->tail == NULL) {
        queue->head = queue->tail = node;
    } else {
        queue->tail->next = node;
        queue->tail = node;
    }
    queue->size++;

    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->lock);
    return 0;
}

/**
 * blocking operation
 */
void *blocking_queue_pop(blocking_queue_t *queue) {
    pthread_mutex_lock(&queue->lock);

    while (queue->size == 0) {
        pthread_cond_wait(&queue->cond, &queue->lock);
    }

    blocking_queue_node_t *temp = queue->head;
    void *data = temp->data;
    queue->head = queue->head->next;
    free(temp);

    if (queue->head == NULL) {
        queue->tail = NULL;
    }

    queue->size--;

    pthread_mutex_unlock(&queue->lock);
    return data;
}

blocking_queue_optional_data blocking_queue_peek(blocking_queue_t *queue, bool can_block) {
    pthread_mutex_lock(&queue->lock);

    while (queue->size == 0) {
        if (!can_block) {
            blocking_queue_optional_data res = {
                    .has_data = false,
                    .data = NULL
            };
            return res;
        }
        pthread_cond_wait(&queue->cond, &queue->lock);
    }

    blocking_queue_optional_data res = {
            .has_data = true,
            .data = queue->head->data
    };
    pthread_mutex_unlock(&queue->lock);
    return res;
}

/**
 * non-blocking operation
 */
void blocking_queue_replace_to_null(blocking_queue_t *queue, void *data) {
    pthread_mutex_lock(&queue->lock);

    if (queue->size == 0) {
        pthread_mutex_unlock(&queue->lock);
        return;
    }

    blocking_queue_node_t *cur = queue->head;
    while (cur) {
        if (cur->data == data) {
            cur->data = NULL;
        }
        cur = cur->next;
    }

    pthread_mutex_unlock(&queue->lock);
}

#endif