#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include "memkind/internal/numus.h"

static struct numamem numamem[MAX_NODES];

bool gather_numamem(void)
{
    FILE *f = fopen("/proc/self/numa_maps", "r");
    if (!f)
        return false;

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *saveptr = 0;
        char *err;
        // We get page size of a mapping only at the end, thus we need to
        // temporarily keep the counts.
        int maxnode = 0;
        short num[MAX_NODES];
        size_t count[MAX_NODES];
        size_t pagesize = 0;

        char *tok = strtok_r(line, " ", &saveptr);
        if (!tok) {
            fprintf(stderr, "Empty line in proc/numa_maps\n");
            fclose(f);
            return false;
        }
        strtoul(tok, &err, 16);
        if (*err) {
            fprintf(stderr, "Invalid addr: %s\n", tok);
            fclose(f);
            return false;
        }
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
                // per-node counts are given as N3=15 which means 15 pages
                // on node 3.
                int node = atoi(tok + 1);
                if (node >= MAX_NODES)
                    continue;
                if (maxnode + 1 >= MAX_NODES) // overflow (can't happen)
                    continue;
                num[maxnode] = node;
                count[maxnode] = atol(valp);
                maxnode++;
            }
        }

        if (maxnode && !pagesize) {
            fprintf(stderr, "Pages used but no page size\n");
            fclose(f);
            return false;
        }
        for (int i = 0; i < maxnode; i++)
            numamem[num[i]].used += count[i] * pagesize;

skip_mapping:
    }
    fclose(f);
    return true;
}

void stringify_numamem(char *buf, int bufsize)
{
    if (!gather_numamem()) {
        snprintf(buf, bufsize, "Couldn't grab numa usage data.\n");
        return;
    }

    for (int i = 0; i < MAX_NODES && bufsize > 0; i++) {
        int len = snprintf(buf, bufsize, " %zu", numamem[i].used / 1024);
        bufsize -= len;
        buf += len;
    }
}
