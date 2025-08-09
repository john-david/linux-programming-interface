
#include "btree.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    btree_t t;
    bt_init(&t);

    bt_add(&t, "d", "delta");
    bt_add(&t, "b", "bravo");
    bt_add(&t, "a", "alpha");
    bt_add(&t, "c", "charlie");
    bt_add(&t, "e", "echo");

    void *val = NULL;
    if (bt_lookup(&t, "c", &val)) {
        printf("c -> %s\n", (char*)val);
    }

    void *old = NULL;
    if (bt_delete(&t, "b", &old) == 0) {
        printf("deleted b (old value: %s)\n", (char*)old);
    }

    bt_destroy(&t, /*free_value=*/NULL);
    return 0;
}

