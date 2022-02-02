#include <stdlib.h>
#include <errno.h>

void __coco_check_bounds(int32_t offset, int32_t array_size){
    if (offset < 0 || offset >= array_size){
        errno = EINVAL;
        exit(errno);
    }
}
