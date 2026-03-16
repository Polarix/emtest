/**
 * @file mem_test.c
 * @brief 内存读写性能测试模块实现
 * @version 1.2
 */

#include "mem_test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <sys/time.h>
#endif

#define MAX_ERROR_MSG_LEN 256

/* ---------- 跨平台高精度计时 ---------- */
#ifdef _WIN32
/**
 * @brief 获取当前时间（微秒）
 * @return 当前时间的微秒数（从某个参考点开始，单调递增）
 */
static uint64_t get_time_us(void)
{
    LARGE_INTEGER freq;
    LARGE_INTEGER count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (uint64_t)(count.QuadPart * 1000000LL / freq.QuadPart);
}
#else
/**
 * @brief 获取当前时间（微秒）
 * @return 当前时间的微秒数（从某个参考点开始，单调递增）
 */
static uint64_t get_time_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}
#endif

struct st_mem_test_context
{
    size_t        total_size;
    size_t        block_size;
    mem_test_mode_t mode;
    unsigned char* buffer;
    size_t        total_bytes;
    size_t        total_blocks;
    size_t        current_block;
    uint64_t      start_us;      /* 开始时间（微秒） */
    uint64_t      end_us;        /* 结束时间（微秒） */
    int           status;        /* 0未开始 1进行中 2已完成 -1错误 */
    mem_test_error_t error_code;
    char          error_msg[MAX_ERROR_MSG_LEN];
};

/* ---------- 内部辅助函数 ---------- */
/**
 * @brief 生成指定块和偏移处的数据模式
 */
static unsigned char generate_pattern(size_t block_index, size_t offset)
{
    return (unsigned char)((block_index & 0xFF) + (offset & 0xFF));
}

/**
 * @brief 用模式填充缓冲区
 */
static void fill_buffer_with_pattern(unsigned char* buffer, size_t block_size, size_t block_index)
{
    for (size_t i = 0; i < block_size; ++i)
    {
        buffer[i] = generate_pattern(block_index, i);
    }
}

/**
 * @brief 验证缓冲区中的数据是否与预期模式一致
 * @return 0成功，-1失败
 */
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

/**
 * @brief 设置错误信息（带errno）
 */
static void set_error(mem_test_ctx* ctx, mem_test_error_t err, const char* msg)
{
    if (ctx == NULL)
    {
        return;
    }
    ctx->error_code = err;
    snprintf(ctx->error_msg, MAX_ERROR_MSG_LEN, "%s (errno=%d)", msg, errno);
}

/* ---------- 公有函数实现 ---------- */
MEMTEST_API mem_test_ctx* mem_test_create(
    size_t total_size,
    size_t block_size,
    mem_test_mode_t mode)
{
    mem_test_ctx* ctx = NULL;
    mem_test_error_t ret = MEM_TEST_ERR_NONE;

    do
    {
        /* 参数校验 */
        if (total_size == 0 || block_size == 0)
        {
            ret = MEM_TEST_ERR_INVALID_PARAM;
            break;
        }
        /* 检查模式是否合法 */
        if (mode != MEM_TEST_MODE_WRITE &&
            mode != MEM_TEST_MODE_READ &&
            mode != MEM_TEST_MODE_VERIFY)
        {
            ret = MEM_TEST_ERR_INVALID_PARAM;
            break;
        }

        ctx = (mem_test_ctx*)calloc(1, sizeof(mem_test_ctx));
        if (ctx == NULL)
        {
            ret = MEM_TEST_ERR_NO_MEMORY;
            break;
        }

        ctx->total_size  = total_size;
        ctx->block_size  = block_size;
        ctx->mode        = mode;
        ctx->buffer      = NULL;
        ctx->total_bytes = 0;
        ctx->total_blocks = (total_size + block_size - 1) / block_size;
        ctx->current_block = 0;
        ctx->start_us    = 0;
        ctx->end_us      = 0;
        ctx->status      = 0;
        ctx->error_code  = MEM_TEST_ERR_NONE;
        ctx->error_msg[0] = '\0';

        ret = MEM_TEST_ERR_NONE;
    } while (0);

    if (ret != MEM_TEST_ERR_NONE && ctx != NULL)
    {
        free(ctx);
        ctx = NULL;
    }
    return ctx;
}

MEMTEST_API void mem_test_destroy(mem_test_ctx* ctx)
{
    if (ctx == NULL)
    {
        return;
    }
    if (ctx->buffer != NULL)
    {
        free(ctx->buffer);
    }
    free(ctx);
}

MEMTEST_API mem_test_error_t mem_test_alloc(mem_test_ctx* ctx)
{
    mem_test_error_t ret = MEM_TEST_ERR_NONE;

    do
    {
        if (ctx == NULL)
        {
            ret = MEM_TEST_ERR_INVALID_PARAM;
            break;
        }
        if (ctx->buffer != NULL)
        {
            set_error(ctx, MEM_TEST_ERR_ALREADY_ALLOCATED, "Memory already allocated");
            ret = MEM_TEST_ERR_ALREADY_ALLOCATED;
            break;
        }

        ctx->buffer = (unsigned char*)malloc(ctx->total_size);
        if (ctx->buffer == NULL)
        {
            set_error(ctx, MEM_TEST_ERR_NO_MEMORY, "Failed to allocate memory");
            ret = MEM_TEST_ERR_NO_MEMORY;
            break;
        }

        ctx->start_us = get_time_us();
        ctx->end_us = ctx->start_us;
        ctx->status = 1;
        ctx->total_bytes = 0;
        ctx->current_block = 0;

        ret = MEM_TEST_ERR_NONE;
    } while (0);

    return ret;
}

MEMTEST_API mem_test_result_t mem_test_process(mem_test_ctx* ctx)
{
    mem_test_result_t ret = MEM_TEST_RESULT_IN_PROGRESS;
    size_t bytes_to_process = 0;

    do
    {
        if (ctx == NULL)
        {
            ret = MEM_TEST_RESULT_ERROR;
            break;
        }
        if (ctx->status != 1)
        {
            ret = (ctx->status == 2) ? MEM_TEST_RESULT_COMPLETED : MEM_TEST_RESULT_ERROR;
            break;
        }
        if (ctx->buffer == NULL)
        {
            set_error(ctx, MEM_TEST_ERR_NOT_ALLOCATED, "Buffer not allocated");
            ctx->status = -1;
            ret = MEM_TEST_RESULT_ERROR;
            break;
        }
        if (ctx->current_block >= ctx->total_blocks)
        {
            ctx->end_us = get_time_us();
            ctx->status = 2;
            ret = MEM_TEST_RESULT_COMPLETED;
            break;
        }

        bytes_to_process = ctx->block_size;
        if (ctx->current_block == ctx->total_blocks - 1)
        {
            size_t remainder = ctx->total_size % ctx->block_size;
            if (remainder != 0)
            {
                bytes_to_process = remainder;
            }
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
                set_error(ctx, MEM_TEST_ERR_VERIFY_FAILED, "Data verification failed");
                ctx->status = -1;
                ret = MEM_TEST_RESULT_ERROR;
                break;
            }
        }
        else
        {
            set_error(ctx, MEM_TEST_ERR_INVALID_PARAM, "Invalid mode");
            ctx->status = -1;
            ret = MEM_TEST_RESULT_ERROR;
            break;
        }

        ctx->total_bytes += bytes_to_process;
        ctx->current_block++;

        if (ctx->current_block >= ctx->total_blocks)
        {
            ctx->end_us = get_time_us();
            ctx->status = 2;
            ret = MEM_TEST_RESULT_COMPLETED;
        }
        else
        {
            ret = MEM_TEST_RESULT_IN_PROGRESS;
        }

    } while (0);

    return ret;
}

MEMTEST_API mem_test_error_t mem_test_free(mem_test_ctx* ctx)
{
    mem_test_error_t ret = MEM_TEST_ERR_NONE;

    do
    {
        if (ctx == NULL)
        {
            ret = MEM_TEST_ERR_INVALID_PARAM;
            break;
        }
        if (ctx->buffer != NULL)
        {
            free(ctx->buffer);
            ctx->buffer = NULL;
        }
        /* 即使没有分配，也视为成功（多次释放无害） */
        ret = MEM_TEST_ERR_NONE;
    } while (0);

    return ret;
}

MEMTEST_API mem_test_error_t mem_test_reset(mem_test_ctx* ctx, mem_test_mode_t mode)
{
    mem_test_error_t ret = MEM_TEST_ERR_NONE;

    do
    {
        if (ctx == NULL)
        {
            ret = MEM_TEST_ERR_INVALID_PARAM;
            break;
        }
        if (ctx->buffer == NULL)
        {
            set_error(ctx, MEM_TEST_ERR_NOT_ALLOCATED, "Buffer not allocated");
            ret = MEM_TEST_ERR_NOT_ALLOCATED;
            break;
        }

        /* 如果 mode 不是保持常量，则更新模式 */
        if (mode != MEM_TEST_MODE_KEEP)
        {
            /* 检查新模式是否合法 */
            if (mode != MEM_TEST_MODE_WRITE &&
                mode != MEM_TEST_MODE_READ &&
                mode != MEM_TEST_MODE_VERIFY)
            {
                set_error(ctx, MEM_TEST_ERR_INVALID_PARAM, "Invalid mode");
                ret = MEM_TEST_ERR_INVALID_PARAM;
                break;
            }
            ctx->mode = mode;
        }

        ctx->status = 1;
        ctx->total_bytes = 0;
        ctx->current_block = 0;
        ctx->start_us = get_time_us();
        ctx->end_us = ctx->start_us;

        ret = MEM_TEST_ERR_NONE;
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

MEMTEST_API mem_test_error_t mem_test_get_throughput(const mem_test_ctx* ctx, double* throughput_mbps)
{
    mem_test_error_t ret = MEM_TEST_ERR_NONE;

    do
    {
        if (ctx == NULL || throughput_mbps == NULL)
        {
            ret = MEM_TEST_ERR_INVALID_PARAM;
            break;
        }
        if (ctx->status != 2)
        {
            ret = MEM_TEST_ERR_NOT_COMPLETED;
            break;
        }

        double time_sec = (ctx->end_us - ctx->start_us) / 1000000.0;
        if (time_sec <= 0.0)
        {
            time_sec = 1e-6;
        }

        *throughput_mbps = (double)ctx->total_bytes / (1024.0 * 1024.0) / time_sec;
        ret = MEM_TEST_ERR_NONE;
    } while (0);

    return ret;
}

MEMTEST_API const char* mem_test_get_error(const mem_test_ctx* ctx)
{
    return (ctx != NULL) ? ctx->error_msg : "No context";
}
