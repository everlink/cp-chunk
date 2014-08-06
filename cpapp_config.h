#ifndef __CPAPP_CONFIG_H
#define __CPAPP_CONFIG_H

/* specifying the maximum file size to transfer */
#define CPAPP_MAX_FILESIZE  (1024 * 1024)

/* the chunk size for each transmission (the max payload size) */
#define CPAPP_MAX_CHUNKSIZE (1024 * 10)
//#define CPAPP_MAX_CHUNKSIZE (256)

/* calculate the size of buffer pool, we are going the allocate the buffers from heap
 * for embedded system should have its implementation in case malloc not supported */
#define CPAPP_BUFF_NUM      (CPAPP_MAX_FILESIZE / CPAPP_MAX_CHUNKSIZE + 1)

#define LEN_FILENAME        255

#endif
