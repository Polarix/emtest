// ==================== disk_test.c ====================
#include "disk_test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>

#ifdef _WIN32
    #include <io.h>          // for _commit, _open, _close, etc.
    #include <windows.h>     // for GetDiskFreeSpaceEx, QueryPerformance*
    #ifndef O_BINARY
        #define O_BINARY 0
    #endif
    #define BINARY_FLAG O_BINARY
    #define fsync _commit
    #define open _open
    #define close _close
    #define read _read
    #define write _write
    #define lseek _lseek
    #define stat _stat
    #define fstat _fstat
    #define ssize_t intptr_t
#else
    #include <sys/statvfs.h> // for statvfs
    #define BINARY_FLAG 0
#endif

#define MAX_FILEPATH_LEN 256
#define PATTERN_BASE      0x55
#define ERROR_MSG_SIZE   256

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
#include <sys/time.h>
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
    uint64_t      start_us;      /* 开始时间（微秒） */
    uint64_t      end_us;        /* 结束时间（微秒） */
    int           status;        /* 0未开始 1进行中 2已完成 -1错误 */
    disk_test_error_t error_code;
    char          error_msg[ERROR_MSG_SIZE];
};

/* ---------- 静态辅助函数 ---------- */
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
static void set_error(disk_test_ctx* ctx, disk_test_error_t err, const char* msg)
{
    if (ctx == NULL)
    {
        return;
    }
    ctx->error_code = err;
    snprintf(ctx->error_msg, ERROR_MSG_SIZE, "%s (errno=%d)", msg, errno);
}

/**
 * @brief 从文件路径中提取目录部分
 * @param path   完整文件路径
 * @param dir_buf 输出缓冲区
 * @param buf_size 缓冲区大小
 * @return 指向 dir_buf 的指针
 */
static const char* get_directory_from_path(const char* path, char* dir_buf, size_t buf_size)
{
    if (path == NULL || dir_buf == NULL || buf_size == 0)
    {
        return ".";
    }

    const char* last_sep = NULL;
    const char* p = path;
    while (*p)
    {
#ifdef _WIN32
        if (*p == '/' || *p == '\\')
#else
        if (*p == '/')
#endif
        {
            last_sep = p;
        }
        p++;
    }

    if (last_sep == NULL)
    {
        /* 路径中没有分隔符，使用当前目录 */
        strncpy(dir_buf, ".", buf_size - 1);
        dir_buf[buf_size - 1] = '\0';
    }
    else
    {
        size_t len = last_sep - path;
        if (len >= buf_size)
        {
            len = buf_size - 1;
        }
        strncpy(dir_buf, path, len);
        dir_buf[len] = '\0';
    }
    return dir_buf;
}

/**
 * @brief 检查指定目录的可用空间是否足够
 * @param dir_path 目录路径
 * @param required 需要的最小字节数
 * @return 0 足够，-1 不足或无法获取
 */
static int check_disk_space(const char* dir_path, uint64_t required)
{
    if (dir_path == NULL || required == 0)
    {
        return -1;
    }

#ifdef _WIN32
    ULARGE_INTEGER free_bytes_available;
    if (!GetDiskFreeSpaceExA(dir_path, &free_bytes_available, NULL, NULL))
    {
        return -1; /* 无法获取空间信息 */
    }
    if (free_bytes_available.QuadPart < required)
    {
        return -1; /* 空间不足 */
    }
#else
    struct statvfs vfs;
    if (statvfs(dir_path, &vfs) != 0)
    {
        return -1;
    }
    uint64_t free_bytes = (uint64_t)vfs.f_frsize * vfs.f_bavail;
    if (free_bytes < required)
    {
        return -1;
    }
#endif
    return 0; /* 空间足够 */
}

/* ---------- 公共函数实现 ---------- */
DISKTEST_API disk_test_ctx* disk_test_create(
    const char* filepath,
    size_t       file_size,
    size_t       block_size,
    disk_test_mode_t mode)
{
    disk_test_ctx* ctx = NULL;
    disk_test_error_t ret = DISK_TEST_ERR_NONE;

    do
    {
        /* 参数校验 */
        if (filepath == NULL || file_size == 0 || block_size == 0)
        {
            ret = DISK_TEST_ERR_INVALID_PARAM;
            break;
        }
        /* 检查模式是否合法 */
        if (mode != DISK_TEST_MODE_WRITE &&
            mode != DISK_TEST_MODE_READ &&
            mode != DISK_TEST_MODE_VERIFY)
        {
            ret = DISK_TEST_ERR_INVALID_PARAM;
            break;
        }

        ctx = (disk_test_ctx*)calloc(1, sizeof(disk_test_ctx));
        if (ctx == NULL)
        {
            ret = DISK_TEST_ERR_NO_MEMORY;
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
        ctx->start_us   = 0;
        ctx->end_us     = 0;
        ctx->status     = 0;
        ctx->error_code = DISK_TEST_ERR_NONE;
        ctx->error_msg[0] = '\0';

        ctx->buffer = (unsigned char*)malloc(block_size);
        if (ctx->buffer == NULL)
        {
            set_error(ctx, DISK_TEST_ERR_NO_MEMORY, "Failed to allocate buffer");
            free(ctx);
            ctx = NULL;
            ret = DISK_TEST_ERR_NO_MEMORY;
            break;
        }

        ret = DISK_TEST_ERR_NONE;
    } while (0);

    /* 若失败且ctx不为NULL，释放内存 */
    if (ret != DISK_TEST_ERR_NONE && ctx != NULL)
    {
        if (ctx->buffer != NULL)
        {
            free(ctx->buffer);
        }
        free(ctx);
        ctx = NULL;
    }
    return ctx;
}

DISKTEST_API void disk_test_destroy(disk_test_ctx* ctx)
{
    if (ctx == NULL)
    {
        return;
    }
    if (ctx->fd != -1)
    {
        close(ctx->fd);
    }
    if (ctx->buffer != NULL)
    {
        free(ctx->buffer);
    }
    free(ctx);
}

DISKTEST_API disk_test_error_t disk_test_open(disk_test_ctx* ctx)
{
    disk_test_error_t ret = DISK_TEST_ERR_NONE;
    int flags = 0;
    mode_t open_mode = S_IRUSR | S_IWUSR;

    do
    {
        if (ctx == NULL)
        {
            ret = DISK_TEST_ERR_INVALID_PARAM;
            break;
        }

        /* 仅写入模式需要检查磁盘空间 */
        if (ctx->mode == DISK_TEST_MODE_WRITE)
        {
            char dir_buf[MAX_FILEPATH_LEN];
            get_directory_from_path(ctx->filepath, dir_buf, sizeof(dir_buf));
            if (check_disk_space(dir_buf, ctx->file_size) != 0)
            {
                set_error(ctx, DISK_TEST_ERR_SPACE_INSUFFICIENT, "Insufficient disk space or cannot query space");
                ret = DISK_TEST_ERR_SPACE_INSUFFICIENT;
                break;
            }
        }

        switch (ctx->mode)
        {
            case DISK_TEST_MODE_WRITE:
                flags = O_WRONLY | O_CREAT | O_TRUNC | BINARY_FLAG;
                break;
            case DISK_TEST_MODE_READ:
            case DISK_TEST_MODE_VERIFY:
                flags = O_RDONLY | BINARY_FLAG;
                break;
            default:
                set_error(ctx, DISK_TEST_ERR_INVALID_PARAM, "Invalid test mode");
                ret = DISK_TEST_ERR_INVALID_PARAM;
                break;
        }
        if (ret != DISK_TEST_ERR_NONE)
        {
            break;
        }

        ctx->fd = open(ctx->filepath, flags, open_mode);
        if (ctx->fd < 0)
        {
            set_error(ctx, DISK_TEST_ERR_OPEN_FAILED, "Failed to open file");
            ret = DISK_TEST_ERR_OPEN_FAILED;
            break;
        }

        /* 对于读/校验模式，检查文件大小是否足够 */
        if (ctx->mode == DISK_TEST_MODE_READ || ctx->mode == DISK_TEST_MODE_VERIFY)
        {
#ifdef _WIN32
            struct _stat st;
            if (_fstat(ctx->fd, &st) != 0)
#else
            struct stat st;
            if (fstat(ctx->fd, &st) != 0)
#endif
            {
                set_error(ctx, DISK_TEST_ERR_STAT_FAILED, "Failed to get file size");
                close(ctx->fd);
                ctx->fd = -1;
                ret = DISK_TEST_ERR_STAT_FAILED;
                break;
            }
            if ((uint64_t)st.st_size < ctx->file_size)
            {
                set_error(ctx, DISK_TEST_ERR_FILE_TOO_SMALL, "File too small for requested test size");
                close(ctx->fd);
                ctx->fd = -1;
                ret = DISK_TEST_ERR_FILE_TOO_SMALL;
                break;
            }
        }

        ctx->start_us = get_time_us();
        ctx->end_us = ctx->start_us;
        ctx->status = 1;
        ctx->total_bytes = 0;
        ctx->current_block = 0;

        ret = DISK_TEST_ERR_NONE;
    } while (0);

    return ret;
}

DISKTEST_API disk_test_result_t disk_test_process(disk_test_ctx* ctx)
{
    disk_test_result_t ret = DISK_TEST_RESULT_IN_PROGRESS;
    ssize_t bytes_io = 0;

    do
    {
        if (ctx == NULL)
        {
            ret = DISK_TEST_RESULT_ERROR;
            break;
        }

        if (ctx->status != 1)
        {
            ret = (ctx->status == 2) ? DISK_TEST_RESULT_COMPLETED : DISK_TEST_RESULT_ERROR;
            break;
        }

        if (ctx->current_block >= ctx->total_blocks)
        {
            ctx->end_us = get_time_us();
            ctx->status = 2;
            ret = DISK_TEST_RESULT_COMPLETED;
            break;
        }

        size_t bytes_to_process = ctx->block_size;
        if (ctx->current_block == ctx->total_blocks - 1)
        {
            size_t remainder = ctx->file_size % ctx->block_size;
            if (remainder != 0)
            {
                bytes_to_process = remainder;
            }
        }

        if (ctx->mode == DISK_TEST_MODE_WRITE)
        {
            fill_buffer_with_pattern(ctx->buffer, bytes_to_process, ctx->current_block);
            bytes_io = write(ctx->fd, ctx->buffer, bytes_to_process);
            if (bytes_io != (ssize_t)bytes_to_process)
            {
                set_error(ctx, DISK_TEST_ERR_WRITE_FAILED, "Write failed or incomplete");
                ctx->status = -1;
                ret = DISK_TEST_RESULT_ERROR;
                break;
            }

            if (fsync(ctx->fd) != 0)
            {
                set_error(ctx, DISK_TEST_ERR_SYNC_FAILED, "Sync failed");
                ctx->status = -1;
                ret = DISK_TEST_RESULT_ERROR;
                break;
            }
        }
        else if (ctx->mode == DISK_TEST_MODE_READ)
        {
            bytes_io = read(ctx->fd, ctx->buffer, bytes_to_process);
            if (bytes_io != (ssize_t)bytes_to_process)
            {
                set_error(ctx, DISK_TEST_ERR_READ_FAILED, "Read failed or incomplete");
                ctx->status = -1;
                ret = DISK_TEST_RESULT_ERROR;
                break;
            }
        }
        else if (ctx->mode == DISK_TEST_MODE_VERIFY)
        {
            bytes_io = read(ctx->fd, ctx->buffer, bytes_to_process);
            if (bytes_io != (ssize_t)bytes_to_process)
            {
                set_error(ctx, DISK_TEST_ERR_READ_FAILED, "Read failed or incomplete during verify");
                ctx->status = -1;
                ret = DISK_TEST_RESULT_ERROR;
                break;
            }
            if (verify_buffer_pattern(ctx->buffer, bytes_to_process, ctx->current_block) != 0)
            {
                set_error(ctx, DISK_TEST_ERR_VERIFY_FAILED, "Data verification failed");
                ctx->status = -1;
                ret = DISK_TEST_RESULT_ERROR;
                break;
            }
        }
        else
        {
            set_error(ctx, DISK_TEST_ERR_INVALID_PARAM, "Invalid mode");
            ctx->status = -1;
            ret = DISK_TEST_RESULT_ERROR;
            break;
        }

        ctx->total_bytes += bytes_io;
        ctx->current_block++;

        if (ctx->current_block >= ctx->total_blocks)
        {
            ctx->end_us = get_time_us();
            ctx->status = 2;
            ret = DISK_TEST_RESULT_COMPLETED;
        }
        else
        {
            ret = DISK_TEST_RESULT_IN_PROGRESS;
        }

    } while (0);

    return ret;
}

DISKTEST_API disk_test_error_t disk_test_close(disk_test_ctx* ctx)
{
    disk_test_error_t ret = DISK_TEST_ERR_NONE;
    do
    {
        if (ctx == NULL)
        {
            ret = DISK_TEST_ERR_INVALID_PARAM;
            break;
        }
        if (ctx->fd != -1)
        {
            if (close(ctx->fd) != 0)
            {
                set_error(ctx, DISK_TEST_ERR_CLOSE_FAILED, "Failed to close file");
                ret = DISK_TEST_ERR_CLOSE_FAILED;
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

DISKTEST_API disk_test_error_t disk_test_get_throughput(const disk_test_ctx* ctx, double* throughput_mbps)
{
    disk_test_error_t ret = DISK_TEST_ERR_NONE;
    do
    {
        if (ctx == NULL || throughput_mbps == NULL)
        {
            ret = DISK_TEST_ERR_INVALID_PARAM;
            break;
        }
        if (ctx->status != 2)
        {
            ret = DISK_TEST_ERR_NOT_COMPLETED;
            break;
        }

        double time_sec = (ctx->end_us - ctx->start_us) / 1000000.0;
        if (time_sec <= 0.0)
        {
            time_sec = 1e-6;
        }

        *throughput_mbps = (double)ctx->total_bytes / (1024.0 * 1024.0) / time_sec;
        ret = DISK_TEST_ERR_NONE;
    } while (0);
    return ret;
}

DISKTEST_API const char* disk_test_get_error(const disk_test_ctx* ctx)
{
    return (ctx != NULL) ? ctx->error_msg : "No context";
}
