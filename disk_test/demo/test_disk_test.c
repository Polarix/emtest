/**
 * @file test_disk_test.c
 * @brief 磁盘测试模块的示例测试程序
 * 
 * 该程序演示如何使用disk_test模块进行写入、读取和校验测试。
 * 支持命令行参数指定文件、大小、块大小和测试类型。
 * 采用循环调用process()直到完成，模拟非阻塞逐步处理。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "disk_test.h"

#define DEFAULT_FILE_SIZE (100 * 1024 * 1024)   /* 100 MB */
#define DEFAULT_BLOCK_SIZE (4 * 1024)           /* 4 KB */
#ifdef _WIN32
#define DEFAULT_TEST_FILE "disk_test.bin"
#else
#define DEFAULT_TEST_FILE "/tmp/disk_test.bin"
#endif

/**
 * @brief 打印使用说明
 */
static void print_usage(const char* progname)
{
    printf("Usage: %s [options]\n", progname);
    printf("Options:\n");
    printf("  -f <file>    Test file path (default: %s)\n", DEFAULT_TEST_FILE);
    printf("  -s <size>    File size in MB (default: %d)\n", DEFAULT_FILE_SIZE / (1024 * 1024));
    printf("  -b <size>    Block size in KB (default: %d)\n", DEFAULT_BLOCK_SIZE / 1024);
    printf("  -m <mode>    Test mode: write, read, verify (default: write+read+verify)\n");
    printf("  -h           Show this help\n");
}

/**
 * @brief 执行指定模式的测试
 * @param filepath 文件路径
 * @param file_size 文件大小
 * @param block_size 块大小
 * @param mode 测试模式
 * @return 0成功，-1失败
 */
static int run_test(const char* filepath, size_t file_size, size_t block_size, disk_test_mode_t mode)
{
    disk_test_ctx* ctx = NULL;
    int ret = 0;
    int process_ret;
    double throughput;

    /* 创建上下文 */
    ctx = disk_test_create(filepath, file_size, block_size, mode);
    if (ctx == NULL)
    {
        fprintf(stderr, "Failed to create test context\n");
        return -1;
    }

    /* 打开文件 */
    if (disk_test_open(ctx) != 0)
    {
        fprintf(stderr, "Failed to open file: %s\n", disk_test_get_error(ctx));
        disk_test_destroy(ctx);
        return -1;
    }

    /* 循环处理直到完成 */
    printf("Starting %s test...\n",
           (mode == DISK_TEST_MODE_WRITE) ? "write" :
           (mode == DISK_TEST_MODE_READ) ? "read" : "verify");
    do
    {
        process_ret = disk_test_process(ctx);
        /* 可选：显示进度 */
        printf("\rProcessed: %zu / %zu bytes (%.1f%%)",
               disk_test_get_processed_bytes(ctx),
               disk_test_get_total_bytes(ctx),
               100.0 * disk_test_get_processed_bytes(ctx) / disk_test_get_total_bytes(ctx));
        fflush(stdout);
    } while (process_ret == 1);

    printf("\n");

    if (process_ret == -1)
    {
        fprintf(stderr, "Test failed: %s\n", disk_test_get_error(ctx));
        ret = -1;
    }
    else
    {
        /* 获取吞吐量 */
        if (disk_test_get_throughput(ctx, &throughput) == 0)
        {
            printf("%s throughput: %.2f MB/s\n",
                   (mode == DISK_TEST_MODE_WRITE) ? "Write" :
                   (mode == DISK_TEST_MODE_READ) ? "Read" : "Verify",
                   throughput);
        }
        else
        {
            printf("Unable to calculate throughput (test incomplete?)\n");
        }
    }

    /* 关闭文件 */
    if (disk_test_close(ctx) != 0)
    {
        fprintf(stderr, "Warning: close failed: %s\n", disk_test_get_error(ctx));
    }

    disk_test_destroy(ctx);
    return ret;
}

int main(int argc, char* argv[])
{
    const char* test_file = DEFAULT_TEST_FILE;
    size_t file_size = DEFAULT_FILE_SIZE;
    size_t block_size = DEFAULT_BLOCK_SIZE;
    int opt;
    int do_write = 1;
    int do_read = 1;
    int do_verify = 1;   /* 默认全部执行 */

    /* 解析命令行参数 */
    while ((opt = getopt(argc, argv, "f:s:b:m:h")) != -1)
    {
        switch (opt)
        {
            case 'f':
                test_file = optarg;
                break;
            case 's':
                file_size = atoi(optarg) * 1024 * 1024;
                if (file_size <= 0)
                {
                    fprintf(stderr, "Invalid file size\n");
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
                /* 简单处理模式，可扩展 */
                do_write = do_read = do_verify = 0;
                if (strcmp(optarg, "write") == 0)
                {
                    do_write = 1;
                }
                else if (strcmp(optarg, "read") == 0)
                {
                    do_read = 1;
                }
                else if (strcmp(optarg, "verify") == 0)
                {
                    do_verify = 1;
                }
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

    printf("Disk Test Demo\n");
    printf("==============\n");
    printf("File: %s\n", test_file);
    printf("Size: %zu MB\n", file_size / (1024 * 1024));
    printf("Block: %zu KB\n", block_size / 1024);

    /* 执行写入测试 */
    if (do_write)
    {
        if (run_test(test_file, file_size, block_size, DISK_TEST_MODE_WRITE) != 0)
        {
            fprintf(stderr, "Write test failed.\n");
            return 1;
        }
        printf("\n");
    }

    /* 执行读取测试 */
    if (do_read)
    {
        if (run_test(test_file, file_size, block_size, DISK_TEST_MODE_READ) != 0)
        {
            fprintf(stderr, "Read test failed.\n");
            return 1;
        }
        printf("\n");
    }

    /* 执行校验测试 */
    if (do_verify)
    {
        if (run_test(test_file, file_size, block_size, DISK_TEST_MODE_VERIFY) != 0)
        {
            fprintf(stderr, "Verify test failed.\n");
            return 1;
        }
        printf("\n");
    }

    /* 清理测试文件（可选） */
    if (remove(test_file) != 0)
    {
        perror("Warning: failed to delete test file");
    }

    printf("All tests completed.\n");
    return 0;
}
