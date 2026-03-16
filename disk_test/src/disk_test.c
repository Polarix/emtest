// ==================== disk_test.c ====================
#include "disk_test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>

#ifdef _WIN32
#include <io.h>   // for _commit
#endif

#define MAX_FILEPATH_LEN 256
#define PATTERN_BASE      0x55

struct st_disk_test_context
{
    char          filepath[MAX_FILEPATH_LEN];
    size_t        file_size;
    size_t        block_size;
    disk_test_mode_t  mode;
    int           fd;
    unsigned char* buffer;
    size_t        total_bytes;
    size_t        total_blocks;
    size_t        current_block;
    struct timeval start_time;
    struct timeval end_time;
    int           status;
    int           error_code;
    char          error_msg[256];
};

static unsigned char generate_pattern(size_t block_index, size_t offset)
{
    return (unsigned char)((block_index & 0xFF) + (offset & 0xFF));
}

static void fill_buffer_with_pattern(unsigned char* buffer, size_t block_size, size_t block_index)
{
    for (size_t i = 0; i < block_size; ++i)
    {
        buffer[i] = generate_pattern(block_index, i);
    }
}

static int verify_buffer_pattern(const unsigned char* buffer, size_t block_size, size_t block_index)
{
    for (size_t i = 0; i < block_size; ++i)
    {
        if (buffer[i] != generate_pattern(block_index, i))
        {
            return -1;
        }
    }
    return 0;
}

static void set_error(disk_test_ctx* ctx, const char* msg)
{
    if (ctx == NULL) return;
    ctx->error_code = errno ? errno : -1;
    strncpy(ctx->error_msg, msg, sizeof(ctx->error_msg) - 1);
    ctx->error_msg[sizeof(ctx->error_msg) - 1] = '\0';
}

DISKTEST_API disk_test_ctx* disk_test_create(
    const char* filepath,
    size_t       file_size,
    size_t       block_size,
    disk_test_mode_t mode)
{
    disk_test_ctx* ctx = NULL;
    int ret = 0;

    do
    {
        if (filepath == NULL || file_size == 0 || block_size == 0)
        {
            ret = -1;
            break;
        }

        ctx = (disk_test_ctx*)calloc(1, sizeof(disk_test_ctx));
        if (ctx == NULL)
        {
            ret = -1;
            break;
        }

        strncpy(ctx->filepath, filepath, MAX_FILEPATH_LEN - 1);
        ctx->filepath[MAX_FILEPATH_LEN - 1] = '\0';
        ctx->file_size  = file_size;
        ctx->block_size = block_size;
        ctx->mode       = mode;
        ctx->fd         = -1;
        ctx->buffer     = NULL;
        ctx->total_bytes = 0;
        ctx->total_blocks = (file_size + block_size - 1) / block_size;
        ctx->current_block = 0;
        ctx->status     = 0;
        ctx->error_code = 0;
        ctx->error_msg[0] = '\0';

        ctx->buffer = (unsigned char*)malloc(block_size);
        if (ctx->buffer == NULL)
        {
            set_error(ctx, "Failed to allocate buffer");
            free(ctx);
            ctx = NULL;
            ret = -1;
            break;
        }

        ret = 0;
    } while (0);

    if (ret != 0 && ctx != NULL)
    {
        if (ctx->buffer != NULL) free(ctx->buffer);
        free(ctx);
        ctx = NULL;
    }
    return ctx;
}

DISKTEST_API void disk_test_destroy(disk_test_ctx* ctx)
{
    if (ctx == NULL) return;
    if (ctx->fd != -1) close(ctx->fd);
    if (ctx->buffer != NULL) free(ctx->buffer);
    free(ctx);
}

DISKTEST_API int disk_test_open(disk_test_ctx* ctx)
{
    int ret = 0;
    int flags = 0;
    mode_t open_mode = S_IRUSR | S_IWUSR;

    do
    {
        if (ctx == NULL)
        {
            ret = -1;
            break;
        }

        switch (ctx->mode)
        {
            case DISK_TEST_MODE_WRITE:
                flags = O_WRONLY | O_CREAT | O_TRUNC;
#ifdef O_BINARY
                flags |= O_BINARY;
#endif
                break;
            case DISK_TEST_MODE_READ:
            case DISK_TEST_MODE_VERIFY:
                flags = O_RDONLY;
#ifdef O_BINARY
                flags |= O_BINARY;
#endif
                break;
            default:
                set_error(ctx, "Invalid test mode");
                ret = -1;
                break;
        }
        if (ret != 0) break;

        ctx->fd = open(ctx->filepath, flags, open_mode);
        if (ctx->fd < 0)
        {
            set_error(ctx, "Failed to open file");
            ret = -1;
            break;
        }

        gettimeofday(&ctx->start_time, NULL);
        ctx->end_time = ctx->start_time;
        ctx->status = 1;
        ctx->total_bytes = 0;
        ctx->current_block = 0;

        ret = 0;
    } while (0);

    return ret;
}

DISKTEST_API int disk_test_process(disk_test_ctx* ctx)
{
    int ret = 1;
    ssize_t bytes_io = 0;

    do
    {
        if (ctx == NULL)
        {
            ret = -1;
            break;
        }

        if (ctx->status != 1)
        {
            ret = (ctx->status == 2) ? 0 : -1;
            break;
        }

        if (ctx->current_block >= ctx->total_blocks)
        {
            gettimeofday(&ctx->end_time, NULL);
            ctx->status = 2;
            ret = 0;
            break;
        }

        size_t bytes_to_process = ctx->block_size;
        if (ctx->current_block == ctx->total_blocks - 1)
        {
            size_t remainder = ctx->file_size % ctx->block_size;
            if (remainder != 0) bytes_to_process = remainder;
        }

        if (ctx->mode == DISK_TEST_MODE_WRITE)
        {
            fill_buffer_with_pattern(ctx->buffer, bytes_to_process, ctx->current_block);
            bytes_io = write(ctx->fd, ctx->buffer, bytes_to_process);
            if (bytes_io != (ssize_t)bytes_to_process)
            {
                set_error(ctx, "Write failed or incomplete");
                ctx->status = -1;
                ret = -1;
                break;
            }

#ifdef _WIN32
            if (_commit(ctx->fd) != 0)
#else
            if (fsync(ctx->fd) != 0)
#endif
            {
                set_error(ctx, "Sync failed");
                ctx->status = -1;
                ret = -1;
                break;
            }
        }
        else if (ctx->mode == DISK_TEST_MODE_READ)
        {
            bytes_io = read(ctx->fd, ctx->buffer, bytes_to_process);
            if (bytes_io != (ssize_t)bytes_to_process)
            {
                set_error(ctx, "Read failed or incomplete");
                ctx->status = -1;
                ret = -1;
                break;
            }
        }
        else if (ctx->mode == DISK_TEST_MODE_VERIFY)
        {
            bytes_io = read(ctx->fd, ctx->buffer, bytes_to_process);
            if (bytes_io != (ssize_t)bytes_to_process)
            {
                set_error(ctx, "Read failed or incomplete during verify");
                ctx->status = -1;
                ret = -1;
                break;
            }
            if (verify_buffer_pattern(ctx->buffer, bytes_to_process, ctx->current_block) != 0)
            {
                set_error(ctx, "Data verification failed");
                ctx->status = -1;
                ret = -1;
                break;
            }
        }
        else
        {
            set_error(ctx, "Invalid mode");
            ctx->status = -1;
            ret = -1;
            break;
        }

        ctx->total_bytes += bytes_io;
        ctx->current_block++;

        if (ctx->current_block >= ctx->total_blocks)
        {
            gettimeofday(&ctx->end_time, NULL);
            ctx->status = 2;
            ret = 0;
        }
        else
        {
            ret = 1;
        }

    } while (0);

    return ret;
}

DISKTEST_API int disk_test_close(disk_test_ctx* ctx)
{
    int ret = 0;
    do
    {
        if (ctx == NULL)
        {
            ret = -1;
            break;
        }
        if (ctx->fd != -1)
        {
            if (close(ctx->fd) != 0)
            {
                set_error(ctx, "Failed to close file");
                ret = -1;
            }
            ctx->fd = -1;
        }
    } while (0);
    return ret;
}

DISKTEST_API size_t disk_test_get_processed_bytes(const disk_test_ctx* ctx)
{
    return (ctx != NULL) ? ctx->total_bytes : 0;
}

DISKTEST_API size_t disk_test_get_total_bytes(const disk_test_ctx* ctx)
{
    return (ctx != NULL) ? ctx->file_size : 0;
}

DISKTEST_API int disk_test_get_throughput(const disk_test_ctx* ctx, double* throughput_mbps)
{
    int ret = -1;
    do
    {
        if (ctx == NULL || throughput_mbps == NULL) break;
        if (ctx->status != 2) break;

        double time_sec = (ctx->end_time.tv_sec - ctx->start_time.tv_sec) +
                          (ctx->end_time.tv_usec - ctx->start_time.tv_usec) / 1000000.0;
        if (time_sec <= 0.0) time_sec = 1e-6;

        *throughput_mbps = (double)ctx->total_bytes / (1024.0 * 1024.0) / time_sec;
        ret = 0;
    } while (0);
    return ret;
}

DISKTEST_API const char* disk_test_get_error(const disk_test_ctx* ctx)
{
    return (ctx != NULL) ? ctx->error_msg : "No context";
}