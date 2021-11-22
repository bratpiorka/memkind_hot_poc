#include <stdint.h>

#define MAX_NODES 1024 /* kernel max */
// If this struct is made dynamic, it'll be numa_num_possible_nodes() long.
struct numamem {
    size_t used;
};

bool gather_numamem(void);
void stringify_numamem(char *buf, int bufsize);
