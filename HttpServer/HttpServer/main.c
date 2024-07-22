#include <stdio.h>
#include "search.h"
#include <stdlib.h>
int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        printf("Usage: %s <filename> respath\n", argv[0]);
        exit(0);
    }
    chdir(argv[2]);
    unsigned short port = atoi(argv[1]);
    epollRun(port);

    return 0;
}
