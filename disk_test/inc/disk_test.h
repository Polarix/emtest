/**
 * @file disk_test.h
 * @brief 磁盘读写性能测试模块（基于帧处理，适用于嵌入式Linux）
 * @version 1.2
 *
 * 提供以固定大小帧为单位进行磁盘写入、读取和校验的功能，
 * 每个函数执行时间短，适合在FSM或GUI框架中逐步调用。
 */

#ifndef DISK_TEST_H
#define DISK_TEST_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 平台相关导出宏 ==================== */
#if defined(_WIN32)
    #ifdef DISKTEST_COMPILING
        /* 正在编译库本身 */
        #ifdef DISKTEST_EXPORTS
            #define DISKTEST_API __declspec(dllexport)
        #else
            #define DISKTEST_API
        #endif
    #else
        /* 用户代码（使用库） */
        #ifdef DISKTEST_STATIC
            #define DISKTEST_API
        #else
            #define DISKTEST_API __declspec(dllimport)
        #endif
    #endif
#else
    #define DISKTEST_API
#endif

/* ==================== 类型定义 ==================== */

/**
 * @brief 测试模式枚举
 */
typedef enum
{
    DISK_TEST_MODE_WRITE = 0,   /**< 写入测试模式 */
    DISK_TEST_MODE_READ,        /**< 读取测试模式（不校验） */
    DISK_TEST_MODE_VERIFY       /**< 校验测试模式（读取并验证数据） */
} disk_test_mode_t;

/**
 * @brief 测试上下文结构体（不透明指针）
 */
typedef struct st_disk_test_context disk_test_ctx;

/* ==================== 公共函数声明 ==================== */

/**
 * @brief 创建测试上下文
 * @param filepath  测试文件路径（字符串会被复制）
 * @param file_size 测试文件总大小（字节）
 * @param block_size 每次处理的块大小（字节）
 * @param mode      测试模式
 * @return 成功返回上下文指针，失败返回NULL
 */
DISKTEST_API disk_test_ctx* disk_test_create(
    const char* filepath,
    size_t       file_size,
    size_t       block_size,
    disk_test_mode_t mode);

/**
 * @brief 销毁测试上下文并释放资源
 * @param ctx 上下文指针（若为NULL则无动作）
 */
DISKTEST_API void disk_test_destroy(disk_test_ctx* ctx);

/**
 * @brief 打开测试文件并准备测试
 * @param ctx 上下文指针
 * @return 0成功，-1失败
 * @note 该函数会记录开始时间，并根据模式创建或打开文件
 */
DISKTEST_API int disk_test_open(disk_test_ctx* ctx);

/**
 * @brief 处理一帧数据（写入/读取/校验一个块）
 * @param ctx 上下文指针
 * @return 1 表示仍在进行中，0 表示所有帧处理完成，-1 表示发生错误
 * @note 该函数执行时间主要受块大小影响，应循环调用直到返回0或-1
 */
DISKTEST_API int disk_test_process(disk_test_ctx* ctx);

/**
 * @brief 关闭测试文件
 * @param ctx 上下文指针
 * @return 0成功，-1失败
 */
DISKTEST_API int disk_test_close(disk_test_ctx* ctx);

/**
 * @brief 获取当前已处理的字节数
 * @param ctx 上下文指针
 * @return 已处理字节数
 */
DISKTEST_API size_t disk_test_get_processed_bytes(const disk_test_ctx* ctx);

/**
 * @brief 获取总文件大小
 * @param ctx 上下文指针
 * @return 总文件大小（字节）
 */
DISKTEST_API size_t disk_test_get_total_bytes(const disk_test_ctx* ctx);

/**
 * @brief 获取测试结果（吞吐量）
 * @param ctx 上下文指针（必须已完成测试）
 * @param throughput_mbps 输出参数：吞吐量（MB/s）
 * @return 0成功，-1失败（测试未完成或尚未计算）
 */
DISKTEST_API int disk_test_get_throughput(const disk_test_ctx* ctx, double* throughput_mbps);

/**
 * @brief 获取最后一次错误信息
 * @param ctx 上下文指针
 * @return 错误信息字符串（内部存储，无需释放）
 */
DISKTEST_API const char* disk_test_get_error(const disk_test_ctx* ctx);

#ifdef __cplusplus
}
#endif

#endif /* DISK_TEST_H */