#ifndef __UIO_H__
#define __UIO_H__

#include "common.h"

struct iovec {
    void *iov_base; /* Starting address */
    size_t iov_len; /* Number of bytes to transfer */
};

#endif // __UIO_H__
