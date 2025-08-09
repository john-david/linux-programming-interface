#define _GNU_SOURCE
#include "btree.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

/* ---- helpers ------------------------------------------------------------ */

static bt_node_t *node_new(const char *key, void *value) {
    bt_node_t *n = calloc(1, sizeof(*n));
    if (!n) return NULL;
    n->key = strdup(key);
    if (!n->key) { free(n); return NULL; }
    n->value = value;
    if (pthread_mutex_init(&n->mtx, NULL) != 0) {
        free(n->key); free(n); return NULL;
    }
    return n;
}

static void node_free(bt_node_t *n, void (*free_value)(void*)) {
    if (!n) return;
    if (free_value) free_value(n->value);
    pthread_mutex_destroy(&n->mtx);
    free(n->key);
    free(n);
}

/* Post-order free; caller must hold tree write-lock. */
static void node_free_recursive(bt_node_t *n, void (*free_value)(void*)) {
    if (!n) return;
    node_free_recursive(n->left, free_value);
    node_free_recursive(n->right, free_value);
    node_free(n, free_value);
}

/* ---- public API --------------------------------------------------------- */

int bt_init(btree_t *t) {
    if (!t) return EINVAL;
    t->root = NULL;
    return pthread_rwlock_init(&t->rwlock, NULL);
}

void bt_destroy(btree_t *t, void (*free_value)(void*)) {
    if (!t) return;
    pthread_rwlock_wrlock(&t->rwlock);
    node_free_recursive(t->root, free_value);
    t->root = NULL;
    pthread_rwlock_unlock(&t->rwlock);
    pthread_rwlock_destroy(&t->rwlock);
}

/* lookup() – readers share the rwlock, no frees can happen concurrently */
bool bt_lookup(btree_t *t, const char *key, void **value_out) {
    if (!t || !key) return false;

    pthread_rwlock_rdlock(&t->rwlock);

    bt_node_t *cur = t->root;
    while (cur) {
        /* Lock this element while we inspect it (as per exercise). */
        pthread_mutex_lock(&cur->mtx);
        int cmp = strcmp(key, cur->key);
        if (cmp == 0) {
            if (value_out) *value_out = cur->value;
            pthread_mutex_unlock(&cur->mtx);
            pthread_rwlock_unlock(&t->rwlock);
            return true;
        }
        bt_node_t *next = (cmp < 0) ? cur->left : cur->right;

        /* Hand-over-hand: lock next before releasing cur?  For a pure reader
           under the tree RD lock it's safe to just drop cur and move on,
           because writers (which could relink) are excluded by the RW lock. */
        pthread_mutex_unlock(&cur->mtx);
        cur = next;
    }

    pthread_rwlock_unlock(&t->rwlock);
    return false;
}

/* add() – writers get the tree write-lock; frees/relinks are exclusive */
int bt_add(btree_t *t, const char *key, void *value) {
    if (!t || !key) return EINVAL;

    int rc = 0;
    pthread_rwlock_wrlock(&t->rwlock);

    if (!t->root) {
        bt_node_t *n = node_new(key, value);
        if (!n) { rc = ENOMEM; goto out; }
        t->root = n;
        goto out;
    }

    bt_node_t *parent = NULL, *cur = t->root;
    pthread_mutex_lock(&cur->mtx);

    for (;;) {
        int cmp = strcmp(key, cur->key);
        if (cmp == 0) {
            /* Replace value; keep key (stable) */
            cur->value = value;
            rc = 1; /* replaced */
            pthread_mutex_unlock(&cur->mtx);
            break;
        }

        bt_node_t **link = (cmp < 0) ? &cur->left : &cur->right;

        if (*link == NULL) {
            /* Insert here */
            bt_node_t *n = node_new(key, value);
            if (!n) { rc = ENOMEM; pthread_mutex_unlock(&cur->mtx); break; }
            *link = n;
            pthread_mutex_unlock(&cur->mtx);
            break;
        }

        /* Move down: lock child, then unlock parent (hand-over-hand) */
        bt_node_t *next = *link;
        pthread_mutex_lock(&next->mtx);
        if (parent) pthread_mutex_unlock(&parent->mtx);
        parent = cur;
        cur = next;
    }

    if (parent) pthread_mutex_unlock(&parent->mtx); /* in case loop exited early */

out:
    pthread_rwlock_unlock(&t->rwlock);
    return rc;
}

/* Helper: find minimum node in a subtree; caller holds write-lock and 'start' locked.
   Returns locked successor node and its parent (parent may be 'start'). */
static void find_min_locked(bt_node_t *start, bt_node_t **min_parent, bt_node_t **min_node) {
    bt_node_t *parent = start;
    bt_node_t *cur = start->right;
    pthread_mutex_lock(&cur->mtx);
    while (cur->left) {
        bt_node_t *next = cur->left;
        pthread_mutex_lock(&next->mtx);
        pthread_mutex_unlock(&parent->mtx);
        parent = cur;
        cur = next;
    }
    *min_parent = parent;
    *min_node = cur;
}

/* delete() – standard BST cases; writer lock excludes readers/writers */
int bt_delete(btree_t *t, const char *key, void **old_value) {
    if (!t || !key) return EINVAL;

    int result = -1;

    pthread_rwlock_wrlock(&t->rwlock);

    bt_node_t *parent = NULL;
    bt_node_t *cur = t->root;
    if (!cur) { result = -1; goto out_unlock; }

    pthread_mutex_lock(&cur->mtx);

    /* Search with hand-over-hand locking */
    while (cur && strcmp(key, cur->key) != 0) {
        int cmp = strcmp(key, cur->key);
        bt_node_t *next = (cmp < 0) ? cur->left : cur->right;
        if (!next) { pthread_mutex_unlock(&cur->mtx); result = -1; goto out_unlock; }
        pthread_mutex_lock(&next->mtx);
        if (parent) pthread_mutex_unlock(&parent->mtx);
        parent = cur;
        cur = next;
    }

    if (!cur) { result = -1; goto out_unlock; } /* not found */

    /* Save value for caller (optional) */
    if (old_value) *old_value = cur->value;

    /* Case 1: node with at most one child */
    if (!cur->left || !cur->right) {
        bt_node_t *child = cur->left ? cur->left : cur->right;
        if (!parent) {
            /* deleting root */
            t->root = child;
        } else {
            /* parent is locked */
            if (parent->left == cur) parent->left = child;
            else parent->right = child;
        }
        pthread_mutex_unlock(&cur->mtx);
        if (parent) pthread_mutex_unlock(&parent->mtx);
        node_free(cur, /*free_value=*/NULL); /* value ownership already handed to caller */
        result = 0;
        goto out_unlock;
    }

    /* Case 2: two children – replace with in-order successor (min in right subtree) */
    bt_node_t *succ_parent = NULL, *succ = NULL;
    find_min_locked(cur, &succ_parent, &succ);
    /* 'cur' and 'succ' and 'succ_parent' are locked; succ has no left child */

    /* Copy successor's key/value into cur (key must remain heap-allocated) */
    char *new_key = strdup(succ->key);
    if (!new_key) {
        /* unwind locks */
        pthread_mutex_unlock(&succ->mtx);
        if (succ_parent) pthread_mutex_unlock(&succ_parent->mtx);
        pthread_mutex_unlock(&cur->mtx);
        if (parent) pthread_mutex_unlock(&parent->mtx);
        result = ENOMEM;
        goto out_unlock;
    }
    free(cur->key);
    cur->key = new_key;
    if (old_value) *old_value = succ->value;  /* hand value of removed node to caller */
    else cur->value = succ->value;            /* or keep successor's value in place */

    /* Splice out successor (succ has no left child) */
    bt_node_t *succ_child = succ->right; /* may be NULL */
    if (succ_parent == cur) {
        cur->right = succ_child;
    } else {
        succ_parent->left = succ_child;
    }

    pthread_mutex_unlock(&succ->mtx);
    pthread_mutex_unlock(&succ_parent->mtx);
    pthread_mutex_unlock(&cur->mtx);
    if (parent) pthread_mutex_unlock(&parent->mtx);

    node_free(succ, /*free_value=*/NULL); /* value ownership already handled */
    result = 0;

out_unlock:
    pthread_rwlock_unlock(&t->rwlock);
    return result;
}



