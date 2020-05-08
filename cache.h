#define MAX_OBJECT_SIZE (100 * 1024)
#define MAX_CACHE_SIZE (1024 * 1024)
typedef __SIZE_TYPE__ size_t;

typedef struct node {
    char *key;
    char *object;
    size_t cache_node_size;
    struct node *prev;
    struct node *next;
} cache_node;

typedef struct {
    size_t queue_size;
    cache_node *head;
    cache_node *tail;
} queue;

queue *create_cache();
void free_queue(queue *cache_queue);
void in_cache(queue *cache_queue, char *key, char *object,
              size_t cache_node_size);
char *read_from_cache(queue *cache_queue, char *uri, size_t *size);
