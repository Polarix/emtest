/**
 * @file mem_test.h
 * @brief 内存读写性能测试模块（基于帧处理，适用于嵌入式系统）
 * @version 1.2
 *
 * 提供以固定大小帧为单位进行内存写入、读取和校验的功能，
 * 每个函数执行时间短，适合在FSM或GUI框架中逐步调用。
 */

#ifndef MEM_TEST_H
#define MEM_TEST_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 平台相关导出宏 ==================== */
#if defined(_WIN32)
    #ifdef MEMTEST_COMPILING
        #ifdef MEMTEST_EXPORTS
            #define MEMTEST_API __declspec(dllexport)
        #else
            #define MEMTEST_API
        #endif
    #else
        #ifdef MEMTEST_STATIC
            #define MEMTEST_API
        #else
            #define MEMTEST_API __declspec(dllimport)
        #endif
    #endif
#else
    #define MEMTEST_API
#endif

/* ==================== 类型定义 ==================== */

/**
 * @brief 测试模式枚举
 */
typedef enum
{
    MEM_TEST_MODE_WRITE = 0,   /**< 写入测试模式（填充数据） */
    MEM_TEST_MODE_READ,        /**< 读取测试模式（不校验） */
    MEM_TEST_MODE_VERIFY       /**< 校验测试模式（读取并验证数据） */
} mem_test_mode_t;

/**
 * @brief 模式保持常量（用于 mem_test_reset 的 mode 参数）
 */
#define MEM_TEST_MODE_KEEP ((mem_test_mode_t)-1)

/**
 * @brief 错误码枚举
 */
typedef enum
{
    MEM_TEST_ERR_NONE = 0,            /**< 成功 */
    MEM_TEST_ERR_INVALID_PARAM,       /**< 无效参数 */
    MEM_TEST_ERR_NO_MEMORY,           /**< 内存不足 */
    MEM_TEST_ERR_ALREADY_ALLOCATED,   /**< 内存已分配 */
    MEM_TEST_ERR_NOT_ALLOCATED,       /**< 内存未分配 */
    MEM_TEST_ERR_VERIFY_FAILED,       /**< 数据校验失败 */
    MEM_TEST_ERR_INVALID_STATE,       /**< 无效状态 */
    MEM_TEST_ERR_NOT_COMPLETED,       /**< 测试未完成 */
    MEM_TEST_ERR_UNKNOWN              /**< 未知错误 */
} mem_test_error_t;

/**
 * @brief 处理结果枚举（用于 mem_test_process）
 */
typedef enum
{
    MEM_TEST_RESULT_ERROR = -1,       /**< 发生错误 */
    MEM_TEST_RESULT_COMPLETED = 0,    /**< 所有帧处理完成 */
    MEM_TEST_RESULT_IN_PROGRESS = 1   /**< 正在处理中 */
} mem_test_result_t;

/**
 * @brief 测试上下文结构体（不透明指针）
 */
typedef struct st_mem_test_context mem_test_ctx;

/* ==================== 公共函数声明 ==================== */

/**
 * @brief 创建测试上下文
 * @param total_size 总内存大小（字节）
 * @param block_size 每次处理的块大小（字节）
 * @param mode       测试模式
 * @return 成功返回上下文指针，失败返回NULL
 */
MEMTEST_API mem_test_ctx* mem_test_create(
    size_t total_size,
    size_t block_size,
    mem_test_mode_t mode);

/**
 * @brief 销毁测试上下文并释放资源
 * @param ctx 上下文指针（若为NULL则无动作）
 */
MEMTEST_API void mem_test_destroy(mem_test_ctx* ctx);

/**
 * @brief 分配测试内存并准备测试
 * @param ctx 上下文指针
 * @return 错误码（MEM_TEST_ERR_NONE 表示成功）
 */
MEMTEST_API mem_test_error_t mem_test_alloc(mem_test_ctx* ctx);

/**
 * @brief 处理一帧数据（写入/读取/校验一个块）
 * @param ctx 上下文指针
 * @return 处理结果：IN_PROGRESS, COMPLETED, ERROR
 */
MEMTEST_API mem_test_result_t mem_test_process(mem_test_ctx* ctx);

/**
 * @brief 释放测试内存
 * @param ctx 上下文指针
 * @return 错误码（MEM_TEST_ERR_NONE 表示成功）
 */
MEMTEST_API mem_test_error_t mem_test_free(mem_test_ctx* ctx);

/**
 * @brief 重置测试上下文，开始新一轮测试
 * @param ctx  上下文指针
 * @param mode 新的测试模式（传入 MEM_TEST_MODE_KEEP 表示保持当前模式）
 * @return 错误码（MEM_TEST_ERR_NONE 表示成功）
 */
MEMTEST_API mem_test_error_t mem_test_reset(mem_test_ctx* ctx, mem_test_mode_t mode);

/**
 * @brief 获取当前已处理的字节数
 * @param ctx 上下文指针
 * @return 已处理字节数
 */
MEMTEST_API size_t mem_test_get_processed_bytes(const mem_test_ctx* ctx);

/**
 * @brief 获取总内存大小
 * @param ctx 上下文指针
 * @return 总内存大小（字节）
 */
MEMTEST_API size_t mem_test_get_total_bytes(const mem_test_ctx* ctx);

/**
 * @brief 获取测试结果（吞吐量）
 * @param ctx 上下文指针（必须已完成测试）
 * @param throughput_mbps 输出参数：吞吐量（MB/s）
 * @return 错误码（MEM_TEST_ERR_NONE 表示成功）
 */
MEMTEST_API mem_test_error_t mem_test_get_throughput(const mem_test_ctx* ctx, double* throughput_mbps);

/**
 * @brief 获取最后一次错误信息
 * @param ctx 上下文指针
 * @return 错误信息字符串（内部存储，无需释放）
 */
MEMTEST_API const char* mem_test_get_error(const mem_test_ctx* ctx);

#ifdef __cplusplus
}
#endif

#endif /* MEM_TEST_H */
