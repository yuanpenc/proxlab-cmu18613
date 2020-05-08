

#include "cache.h"
#include "csapp.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>

static pthread_mutex_t mutex;

queue *create_cache();
bool hit(char *key, queue *cache_queue);
cache_node *create_new_node(char *key, char *object, size_t cache_node_size);
void remove_last(queue *cache_queue);
void evicte(queue *cache_queue, cache_node *cur_node);
void insert(queue *cache_queue, cache_node *cur_node);
void in_cache(queue *cache_queue, char *key, char *object,
              size_t cache_node_size);
void free_queue(queue *cache_queue);
void move_to_front(queue *cache_queue, cache_node *cur_node);
char *read_from_cache(queue *cache_queue, char *uri, size_t *size);

char *read_from_cache(queue *cache_queue, char *uri, size_t *size) {
    pthread_mutex_lock(&mutex);
    cache_node *cur_node;
    cache_node *result = NULL;
    for (cur_node = cache_queue->head->next; cur_node != cache_queue->tail;
         cur_node = cur_node->next) {
        if (strcmp(cur_node->key, uri) == 0) {
            result = cur_node;
            move_to_front(cache_queue, cur_node);
            break;
        }
    }

    if (result == NULL) {
        pthread_mutex_unlock(&mutex);
        return NULL;
    }

   

    // printf("size = %lu \n", *size);
    char *response = (char *)malloc(sizeof(char) * (result->cache_node_size));
    *size = result->cache_node_size;
    memcpy(response, result->object, result->cache_node_size);
    // printf("!respo !!!!! ===== %s!\n", response);
    pthread_mutex_unlock(&mutex);
    return response;
}

void move_to_front(queue *cache_queue, cache_node *cur_node) {
    cur_node->next->prev = cur_node->prev;
    cur_node->prev->next = cur_node->next;

    cache_node *head = cache_queue->head;
    head->next->prev = cur_node;
    cur_node->next = head->next;
    head->next = cur_node;
    cur_node->prev = head;
    return;
}

void free_queue(queue *cache_queue) {
    cache_node *cur_node = cache_queue->tail->prev;
    while (cur_node != cache_queue->head) {
        remove_last(cache_queue);
    }
    Free(cache_queue->tail);
    Free(cache_queue->head);
    Free(cache_queue);
    return;
}

queue *create_cache() {
    queue *cache_queue = (queue *)malloc(sizeof(queue));
    cache_queue->queue_size = 0;

    cache_node *head = (cache_node *)malloc(sizeof(cache_node));
    head->next = NULL;
    head->prev = NULL;
    head->key = NULL;
    head->object = NULL;
    head->cache_node_size = 0;
    cache_queue->head = head;

    cache_node *tail = (cache_node *)malloc(sizeof(cache_node));
    tail->cache_node_size = 0;
    tail->key = NULL;
    tail->next = NULL;
    tail->object = NULL;
    cache_queue->tail = tail;

    cache_queue->tail->prev = cache_queue->head;
    cache_queue->head->next = cache_queue->tail;

    pthread_mutex_init(&mutex, NULL);

    return cache_queue;
}

bool hit(char *key, queue *cache_queue) {
    cache_node *cur_node = cache_queue->head->next;

    while (cur_node != cache_queue->tail) {
        if (strcmp(cur_node->key, key) == 0) {
            return true;
        }
        cur_node = cur_node->next;
    }
    return false;
}

cache_node *create_new_node(char *key, char *object, size_t cache_node_size) {
    cache_node *new_node = (cache_node *)malloc(sizeof(cache_node));
    new_node->cache_node_size = cache_node_size;
    new_node->key = (char *)malloc(sizeof(char) * (strlen(key) + 1));
    strcpy(new_node->key, key);

    new_node->object = (char *)malloc(sizeof(char) * cache_node_size);
    // TODO:use memcpy?
    memcpy(new_node->object, object, sizeof(char) * cache_node_size);
    return new_node;
}

void remove_last(queue *cache_queue) {
    // decrease size
    cache_node *last = cache_queue->tail->prev;
    cache_queue->queue_size = cache_queue->queue_size - last->cache_node_size;
    if (cache_queue->queue_size < 0)
        printf("error queue size");
    last->prev->next = last->next;
    last->next->prev = last->prev;

    Free(last->key);
    Free(last->object);
    Free(last);
    return;
}

void evicte(queue *cache_queue, cache_node *cur_node) {
    cache_node *last_node;
    last_node = cache_queue->tail->prev;

    while (last_node != cache_queue->head) {
        remove_last(cache_queue);
        size_t cur_size = cur_node->cache_node_size + cache_queue->queue_size;
        if (cur_size < MAX_CACHE_SIZE) {
            insert(cache_queue, cur_node);
            break;
        }
        last_node = cache_queue->tail->prev;
    }

    return;
}

void insert(queue *cache_queue, cache_node *cur_node) {
    // increase size
    cache_queue->queue_size += cur_node->cache_node_size;

    // insert in the front
    cache_node *next = cache_queue->head->next;
    next->prev = cur_node;
    cur_node->next = next;
    cur_node->prev = cache_queue->head;
    cache_queue->head->next = cur_node;

    return;
}

void in_cache(queue *cache_queue, char *key, char *object,
              size_t cache_node_size) {

    if (hit(key, cache_queue))
        return;

    pthread_mutex_lock(&mutex);

    size_t cur_size = cache_queue->queue_size + cache_node_size;

    cache_node *cur_node = create_new_node(key, object, cache_node_size);

    if (cur_size > MAX_CACHE_SIZE) {
        evicte(cache_queue, cur_node);
    } else {
        insert(cache_queue, cur_node);
    }

    pthread_mutex_unlock(&mutex);

    return;
}
