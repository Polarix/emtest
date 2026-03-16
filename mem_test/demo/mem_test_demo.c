/**
 * @file mem_test_demo.c
 * @brief 内存测试模块的示例测试程序
 *
 * 该程序演示如何使用mem_test模块进行写入、读取和校验测试。
 * 支持命令行参数指定内存大小、块大小和测试类型。
 * 采用循环调用process()直到完成，模拟非阻塞逐步处理。
 *
 * 如果没有使用 -m 指定模式，则默认按顺序执行写入、读取、验证，
 * 且共享同一块内存，确保验证有意义。
 * 如果使用 -m 指定单一模式，则每次测试独立分配内存。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "mem_test.h"

#ifdef _WIN32
#include <getopt.h>
#endif

#define DEFAULT_TOTAL_SIZE (32 * 1024 * 1024)   /* 32 MB */
#define DEFAULT_BLOCK_SIZE (4 * 1024)           /* 4 KB */

/**
 * @brief 打印使用说明
 */
static void print_usage(const char* progname)
{
    printf("Usage: %s [options]\n", progname);
    printf("Options:\n");
    printf("  -s <size>    Total memory size in MB (default: %d)\n", DEFAULT_TOTAL_SIZE / (1024 * 1024));
    printf("  -b <size>    Block size in KB (default: %d)\n", DEFAULT_BLOCK_SIZE / 1024);
    printf("  -m <mode>    Test mode: write, read, verify (if omitted, runs all three sequentially)\n");
    printf("  -h           Show this help\n");
}

/**
 * @brief 执行独立分配内存的单个模式测试
 */
static int run_test_independent(size_t total_size, size_t block_size, mem_test_mode_t mode)
{
    mem_test_ctx* ctx = NULL;
    mem_test_result_t process_ret;
    double throughput;
    int ret = 0;

    ctx = mem_test_create(total_size, block_size, mode);
    if (ctx == NULL)
    {
        fprintf(stderr, "Failed to create test context\n");
        return -1;
    }

    if (mem_test_alloc(ctx) != MEM_TEST_ERR_NONE)
    {
        fprintf(stderr, "Failed to allocate memory: %s\n", mem_test_get_error(ctx));
        mem_test_destroy(ctx);
        return -1;
    }

    printf("Starting %s test (independent)...\n",
           (mode == MEM_TEST_MODE_WRITE) ? "write" :
           (mode == MEM_TEST_MODE_READ) ? "read" : "verify");
    do
    {
        process_ret = mem_test_process(ctx);
        printf("\rProcessed: %zu / %zu bytes (%.1f%%)",
               mem_test_get_processed_bytes(ctx),
               mem_test_get_total_bytes(ctx),
               100.0 * mem_test_get_processed_bytes(ctx) / mem_test_get_total_bytes(ctx));
        fflush(stdout);
    } while (process_ret == MEM_TEST_RESULT_IN_PROGRESS);
    printf("\n");

    if (process_ret == MEM_TEST_RESULT_ERROR)
    {
        fprintf(stderr, "Test failed: %s\n", mem_test_get_error(ctx));
        ret = -1;
    }
    else
    {
        if (mem_test_get_throughput(ctx, &throughput) == MEM_TEST_ERR_NONE)
        {
            printf("%s throughput: %.2f MB/s\n",
                   (mode == MEM_TEST_MODE_WRITE) ? "Write" :
                   (mode == MEM_TEST_MODE_READ) ? "Read" : "Verify",
                   throughput);
        }
    }

    mem_test_free(ctx);
    mem_test_destroy(ctx);
    return ret;
}

/**
 * @brief 执行共享同一块内存的顺序测试（写、读、验证）
 */
static int run_test_shared(size_t total_size, size_t block_size)
{
    mem_test_ctx* ctx = NULL;
    mem_test_result_t process_ret;
    double throughput;
    int ret = 0;

    ctx = mem_test_create(total_size, block_size, MEM_TEST_MODE_WRITE);
    if (ctx == NULL)
    {
        fprintf(stderr, "Failed to create test context\n");
        return -1;
    }

    if (mem_test_alloc(ctx) != MEM_TEST_ERR_NONE)
    {
        fprintf(stderr, "Failed to allocate memory: %s\n", mem_test_get_error(ctx));
        mem_test_destroy(ctx);
        return -1;
    }

    /* 写入测试 */
    printf("Starting write test...\n");
    do
    {
        process_ret = mem_test_process(ctx);
        printf("\rProcessed: %zu / %zu bytes (%.1f%%)",
               mem_test_get_processed_bytes(ctx),
               mem_test_get_total_bytes(ctx),
               100.0 * mem_test_get_processed_bytes(ctx) / mem_test_get_total_bytes(ctx));
        fflush(stdout);
    } while (process_ret == MEM_TEST_RESULT_IN_PROGRESS);
    printf("\n");

    if (process_ret == MEM_TEST_RESULT_ERROR)
    {
        fprintf(stderr, "Write test failed: %s\n", mem_test_get_error(ctx));
        ret = -1;
        goto cleanup;
    }

    if (mem_test_get_throughput(ctx, &throughput) == MEM_TEST_ERR_NONE)
        printf("Write throughput: %.2f MB/s\n\n", throughput);

    /* 重置为读取模式 */
    if (mem_test_reset(ctx, MEM_TEST_MODE_READ) != MEM_TEST_ERR_NONE)
    {
        fprintf(stderr, "Failed to reset context for read: %s\n", mem_test_get_error(ctx));
        ret = -1;
        goto cleanup;
    }

    printf("Starting read test...\n");
    do
    {
        process_ret = mem_test_process(ctx);
        printf("\rProcessed: %zu / %zu bytes (%.1f%%)",
               mem_test_get_processed_bytes(ctx),
               mem_test_get_total_bytes(ctx),
               100.0 * mem_test_get_processed_bytes(ctx) / mem_test_get_total_bytes(ctx));
        fflush(stdout);
    } while (process_ret == MEM_TEST_RESULT_IN_PROGRESS);
    printf("\n");

    if (process_ret == MEM_TEST_RESULT_ERROR)
    {
        fprintf(stderr, "Read test failed: %s\n", mem_test_get_error(ctx));
        ret = -1;
        goto cleanup;
    }

    if (mem_test_get_throughput(ctx, &throughput) == MEM_TEST_ERR_NONE)
        printf("Read throughput: %.2f MB/s\n\n", throughput);

    /* 重置为验证模式 */
    if (mem_test_reset(ctx, MEM_TEST_MODE_VERIFY) != MEM_TEST_ERR_NONE)
    {
        fprintf(stderr, "Failed to reset context for verify: %s\n", mem_test_get_error(ctx));
        ret = -1;
        goto cleanup;
    }

    printf("Starting verify test...\n");
    do
    {
        process_ret = mem_test_process(ctx);
        printf("\rProcessed: %zu / %zu bytes (%.1f%%)",
               mem_test_get_processed_bytes(ctx),
               mem_test_get_total_bytes(ctx),
               100.0 * mem_test_get_processed_bytes(ctx) / mem_test_get_total_bytes(ctx));
        fflush(stdout);
    } while (process_ret == MEM_TEST_RESULT_IN_PROGRESS);
    printf("\n");

    if (process_ret == MEM_TEST_RESULT_ERROR)
    {
        fprintf(stderr, "Verify test failed: %s\n", mem_test_get_error(ctx));
        ret = -1;
        goto cleanup;
    }

    if (mem_test_get_throughput(ctx, &throughput) == MEM_TEST_ERR_NONE)
        printf("Verify throughput: %.2f MB/s\n", throughput);

cleanup:
    mem_test_free(ctx);
    mem_test_destroy(ctx);
    return ret;
}

int main(int argc, char* argv[])
{
    size_t total_size = DEFAULT_TOTAL_SIZE;
    size_t block_size = DEFAULT_BLOCK_SIZE;
    int opt;
    int do_write = 0, do_read = 0, do_verify = 0;
    int single_mode_selected = 0;

    while ((opt = getopt(argc, argv, "s:b:m:h")) != -1)
    {
        switch (opt)
        {
            case 's':
                total_size = atoi(optarg) * 1024 * 1024;
                if (total_size <= 0)
                {
                    fprintf(stderr, "Invalid total size\n");
                    return 1;
                }
                break;
            case 'b':
                block_size = atoi(optarg) * 1024;
                if (block_size <= 0)
                {
                    fprintf(stderr, "Invalid block size\n");
                    return 1;
                }
                break;
            case 'm':
                single_mode_selected = 1;
                if (strcmp(optarg, "write") == 0) do_write = 1;
                else if (strcmp(optarg, "read") == 0) do_read = 1;
                else if (strcmp(optarg, "verify") == 0) do_verify = 1;
                else
                {
                    fprintf(stderr, "Unknown mode: %s\n", optarg);
                    print_usage(argv[0]);
                    return 1;
                }
                break;
            case 'h':
            default:
                print_usage(argv[0]);
                return (opt == 'h') ? 0 : 1;
        }
    }

    if (!do_write && !do_read && !do_verify)
    {
        single_mode_selected = 0;
        do_write = do_read = do_verify = 1;
    }

    printf("Memory Test Demo\n");
    printf("================\n");
    printf("Size: %zu MB\n", total_size / (1024 * 1024));
    printf("Block: %zu KB\n", block_size / 1024);

    int ret = 0;

    if (single_mode_selected)
    {
        if (do_write)
        {
            if (run_test_independent(total_size, block_size, MEM_TEST_MODE_WRITE) != 0)
                ret = 1;
            printf("\n");
        }
        if (do_read)
        {
            if (run_test_independent(total_size, block_size, MEM_TEST_MODE_READ) != 0)
                ret = 1;
            printf("\n");
        }
        if (do_verify)
        {
            if (run_test_independent(total_size, block_size, MEM_TEST_MODE_VERIFY) != 0)
                ret = 1;
            printf("\n");
        }
    }
    else
    {
        if (run_test_shared(total_size, block_size) != 0)
            ret = 1;
    }

    if (ret == 0)
        printf("All tests completed.\n");
    return ret;
}
