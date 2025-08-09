
#ifndef BTREE_H
#define BTREE_H

#include <stdbool.h>
#include <pthread.h>

typedef struct bt_node {
    char *key;
    void *value;
    struct bt_node *left;
    struct bt_node *right;
    pthread_mutex_t mtx;   /* Protects this node's fields */
} bt_node_t;

typedef struct {
    bt_node_t *root;
    pthread_rwlock_t rwlock; /* Protects structural changes (relink/free) */
} btree_t;

/* Initialize/destroy the tree */
int  bt_init(btree_t *t);
void bt_destroy(btree_t *t, void (*free_value)(void*));

/* CRUD */
int   bt_add(btree_t *t, const char *key, void *value);     /* 0=inserted, 1=replaced, <0 on error */
int   bt_delete(btree_t *t, const char *key, void **old_value); /* 0=deleted, -1=not found */
bool  bt_lookup(btree_t *t, const char *key, void **value_out);

#endif /* BTREE_H */

