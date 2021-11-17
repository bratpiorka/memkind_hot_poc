#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#define MAX_NODES 1024 /* kernel max */
static struct {
    size_t used;
} numamem[MAX_NODES];

static bool gather_numamem()
{
    FILE *f = fopen("/proc/self/numa_maps", "r");
    if (!f)
        return false;

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *saveptr = 0;
        char *err;
        // we get page size of a mapping only at the end
        int maxnode = 0;
        short num[MAX_NODES];
        size_t count[MAX_NODES];
        uint64_t pagesize = 0;

        char *tok = strtok_r(line, " ", &saveptr);
        if (!tok)
            return fprintf(stderr, "Empty line in proc/numa_maps\n"), false;
        strtoul(tok, &err, 16);
        if (*err)
            return fprintf(stderr, "Invalid addr: %s\n", tok), false;
        //printf("Addr: %lx\n", val);

        while ((tok = strtok_r(0, " ", &saveptr))) {
            char *valp = strchr(tok, '=');
            if (!valp) {
                /* flags */
                if (!strcmp(tok, "stack"))
                    goto skip_mapping;
                continue;
            }
            *valp++ = 0;

            if (!strcmp(tok, "kernelpagesize_kB"))
                pagesize = atoi(valp) * 1024ULL;
            else if (tok[0] == 'N' && tok[1] >= '0' && tok[1] <= '9') {
                int node = atoi(tok + 1);
                if (node >= MAX_NODES)
                    continue;
                if (maxnode + 1 >= MAX_NODES)
                    continue;
                num[maxnode] = node;
                count[maxnode] = atol(valp);
                maxnode++;
            }
        }

        if (maxnode && !pagesize)
            return fprintf(stderr, "Pages used but no page size\n"), false;
        for (int i = 0; i < maxnode; i++)
            numamem[num[i]].used += count[i] * pagesize;

skip_mapping:
    }
    fclose(f);
    return true;
}
