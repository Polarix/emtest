/**
 * @file mem_test.h
 * @brief 内存读写性能测试模块（基于帧处理，适用于嵌入式系统）
 * @version 1.1
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

typedef enum
{
    MEM_TEST_MODE_WRITE = 0,   /**< 写入测试模式（填充数据） */
    MEM_TEST_MODE_READ,        /**< 读取测试模式（不校验） */
    MEM_TEST_MODE_VERIFY       /**< 校验测试模式（读取并验证数据） */
} mem_test_mode_t;

typedef struct st_mem_test_context mem_test_ctx;

/* ==================== 公共函数声明 ==================== */

MEMTEST_API mem_test_ctx* mem_test_create(
    size_t total_size,
    size_t block_size,
    mem_test_mode_t mode);

MEMTEST_API void mem_test_destroy(mem_test_ctx* ctx);

MEMTEST_API int mem_test_alloc(mem_test_ctx* ctx);

MEMTEST_API int mem_test_process(mem_test_ctx* ctx);

MEMTEST_API int mem_test_free(mem_test_ctx* ctx);

/**
 * @brief 重置测试上下文，开始新一轮测试
 * @param ctx 上下文指针
 * @param mode 新的测试模式（若保持当前模式可传入 -1）
 * @return 0成功，-1失败（例如 ctx 无效或缓冲区未分配）
 */
MEMTEST_API int mem_test_reset(mem_test_ctx* ctx, mem_test_mode_t mode);

MEMTEST_API size_t mem_test_get_processed_bytes(const mem_test_ctx* ctx);

MEMTEST_API size_t mem_test_get_total_bytes(const mem_test_ctx* ctx);

MEMTEST_API int mem_test_get_throughput(const mem_test_ctx* ctx, double* throughput_mbps);

MEMTEST_API const char* mem_test_get_error(const mem_test_ctx* ctx);

#ifdef __cplusplus
}
#endif

#endif /* MEM_TEST_H */
