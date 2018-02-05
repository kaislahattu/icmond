#include <stdio.h>
#include <errno.h>

#include "../tmpfs.h"
#include "../logwrite.h"

#define MOUNTPOINT "/tmp/icmond.tmpfs"
int main()
{
    if (tmpfs_mount(MOUNTPOINT, 3))
    {
        if (errno == EINVAL)
            printf("tmpfs_mount() argument(s) invalid!\n");
        else if (errno == EBUSY)
            printf("Specified mountpoint \"%s\" already has something mounted on it!\n", MOUNTPOINT);
        else
            printf("Unrecognized errno!\n");
        return 1;
    }
    return 1;
}

