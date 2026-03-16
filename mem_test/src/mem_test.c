/**
 * @file mem_test.c
 * @brief 内存读写性能测试模块实现
 * @version 1.1
 */

#include "mem_test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <errno.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define MAX_ERROR_MSG_LEN 256

struct st_mem_test_context
{
    size_t        total_size;
    size_t        block_size;
    mem_test_mode_t mode;
    unsigned char* buffer;
    size_t        total_bytes;
    size_t        total_blocks;
    size_t        current_block;
    struct timeval start_time;
    struct timeval end_time;
    int           status;      /* 0未开始 1进行中 2已完成 -1错误 */
    int           error_code;
    char          error_msg[MAX_ERROR_MSG_LEN];
};

/* ---------- 内部辅助函数 ---------- */
static unsigned long get_time_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000000UL) + tv.tv_usec;
}

static unsigned char generate_pattern(size_t block_index, size_t offset)
{
    return (unsigned char)((block_index & 0xFF) + (offset & 0xFF));
}

static void fill_buffer_with_pattern(unsigned char* buffer, size_t block_size, size_t block_index)
{
    for (size_t i = 0; i < block_size; ++i)
        buffer[i] = generate_pattern(block_index, i);
}

static int verify_buffer_pattern(const unsigned char* buffer, size_t block_size, size_t block_index)
{
    for (size_t i = 0; i < block_size; ++i)
        if (buffer[i] != generate_pattern(block_index, i))
            return -1;
    return 0;
}

static void set_error(mem_test_ctx* ctx, const char* msg)
{
    if (ctx == NULL) return;
    ctx->error_code = errno ? errno : -1;
    strncpy(ctx->error_msg, msg, MAX_ERROR_MSG_LEN - 1);
    ctx->error_msg[MAX_ERROR_MSG_LEN - 1] = '\0';
}

/* ---------- 公有函数实现 ---------- */
MEMTEST_API mem_test_ctx* mem_test_create(
    size_t total_size,
    size_t block_size,
    mem_test_mode_t mode)
{
    mem_test_ctx* ctx = NULL;
    int ret = 0;

    do
    {
        if (total_size == 0 || block_size == 0)
        {
            ret = -1;
            break;
        }

        ctx = (mem_test_ctx*)calloc(1, sizeof(mem_test_ctx));
        if (ctx == NULL)
        {
            ret = -1;
            break;
        }

        ctx->total_size  = total_size;
        ctx->block_size  = block_size;
        ctx->mode        = mode;
        ctx->buffer      = NULL;
        ctx->total_bytes = 0;
        ctx->total_blocks = (total_size + block_size - 1) / block_size;
        ctx->current_block = 0;
        ctx->status      = 0;
        ctx->error_code  = 0;
        ctx->error_msg[0] = '\0';

        ret = 0;
    } while (0);

    if (ret != 0 && ctx != NULL)
    {
        free(ctx);
        ctx = NULL;
    }
    return ctx;
}

MEMTEST_API void mem_test_destroy(mem_test_ctx* ctx)
{
    if (ctx == NULL) return;
    if (ctx->buffer != NULL) free(ctx->buffer);
    free(ctx);
}

MEMTEST_API int mem_test_alloc(mem_test_ctx* ctx)
{
    int ret = 0;

    do
    {
        if (ctx == NULL)
        {
            ret = -1;
            break;
        }
        if (ctx->buffer != NULL)
        {
            set_error(ctx, "Memory already allocated");
            ret = -1;
            break;
        }

        ctx->buffer = (unsigned char*)malloc(ctx->total_size);
        if (ctx->buffer == NULL)
        {
            set_error(ctx, "Failed to allocate memory");
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

MEMTEST_API int mem_test_process(mem_test_ctx* ctx)
{
    int ret = 1;
    size_t bytes_to_process = 0;

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

        bytes_to_process = ctx->block_size;
        if (ctx->current_block == ctx->total_blocks - 1)
        {
            size_t remainder = ctx->total_size % ctx->block_size;
            if (remainder != 0) bytes_to_process = remainder;
        }

        if (ctx->mode == MEM_TEST_MODE_WRITE)
        {
            fill_buffer_with_pattern(ctx->buffer + ctx->total_bytes,
                                     bytes_to_process,
                                     ctx->current_block);
        }
        else if (ctx->mode == MEM_TEST_MODE_READ)
        {
            /* 模拟读取：每个字节访问一次，避免编译器优化 */
            volatile unsigned char temp;
            for (size_t i = 0; i < bytes_to_process; ++i)
            {
                temp = ctx->buffer[ctx->total_bytes + i];
                (void)temp;
            }
        }
        else if (ctx->mode == MEM_TEST_MODE_VERIFY)
        {
            if (verify_buffer_pattern(ctx->buffer + ctx->total_bytes,
                                      bytes_to_process,
                                      ctx->current_block) != 0)
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

        ctx->total_bytes += bytes_to_process;
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

MEMTEST_API int mem_test_free(mem_test_ctx* ctx)
{
    int ret = 0;
    do
    {
        if (ctx == NULL)
        {
            ret = -1;
            break;
        }
        if (ctx->buffer != NULL)
        {
            free(ctx->buffer);
            ctx->buffer = NULL;
        }
    } while (0);
    return ret;
}

MEMTEST_API int mem_test_reset(mem_test_ctx* ctx, mem_test_mode_t mode)
{
    int ret = 0;
    do
    {
        if (ctx == NULL)
        {
            ret = -1;
            break;
        }
        if (ctx->buffer == NULL)
        {
            set_error(ctx, "Buffer not allocated");
            ret = -1;
            break;
        }
        if (mode != (mem_test_mode_t)-1)
        {
            ctx->mode = mode;
        }
        ctx->status = 1;
        ctx->total_bytes = 0;
        ctx->current_block = 0;
        gettimeofday(&ctx->start_time, NULL);
        ctx->end_time = ctx->start_time;
        ret = 0;
    } while (0);
    return ret;
}

MEMTEST_API size_t mem_test_get_processed_bytes(const mem_test_ctx* ctx)
{
    return (ctx != NULL) ? ctx->total_bytes : 0;
}

MEMTEST_API size_t mem_test_get_total_bytes(const mem_test_ctx* ctx)
{
    return (ctx != NULL) ? ctx->total_size : 0;
}

MEMTEST_API int mem_test_get_throughput(const mem_test_ctx* ctx, double* throughput_mbps)
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

MEMTEST_API const char* mem_test_get_error(const mem_test_ctx* ctx)
{
    return (ctx != NULL) ? ctx->error_msg : "No context";
}
