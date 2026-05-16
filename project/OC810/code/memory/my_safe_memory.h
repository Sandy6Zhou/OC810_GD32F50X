/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       my_safe_memory.h
**文件描述：       安全内存管理辅助函数和宏定义
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.04.17
*********************************************************************
** 功能描述：       1. 提供统一的内存分配、释放和检查机制
**                 2. 分配失败时自动输出错误日志并返回
**                 3. 防止Double Free和空指针访问
**                 4. 提供内存健康监控接口
*********************************************************************/

#ifndef __MY_SAFE_MEMORY_H
#define __MY_SAFE_MEMORY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "FreeRTOS.h"
#include "task.h"
#include "my_log.h"

/* 内存分配失败时的返回值 */
#define MY_MEM_ALLOC_FAILED    NULL

/* 安全内存分配宏：失败时输出错误日志并返回 */
#define MY_SAFE_MALLOC(ptr, size, ret_val) \
    do { \
        (ptr) = pvPortMalloc(size); \
        if ((ptr) == NULL) \
        { \
            MY_LOG_E("[MEM] Alloc failed: %d bytes at %s:%d", (size), __FILE__, __LINE__); \
            return (ret_val); \
        } \
    } while (0)

/* 安全内存分配宏（goto版）：失败时输出错误日志并跳转到标签 */
#define MY_SAFE_MALLOC_GOTO(ptr, size, label) \
    do { \
        (ptr) = pvPortMalloc(size); \
        if ((ptr) == NULL) \
        { \
            MY_LOG_E("[MEM] Alloc failed: %d bytes at %s:%d", (size), __FILE__, __LINE__); \
            goto label; \
        } \
    } while (0)

/* 安全内存分配宏（带标签）：失败时输出带标签的错误日志并返回 */
#define MY_SAFE_MALLOC_TAG(ptr, size, tag, ret_val) \
    do { \
        (ptr) = pvPortMalloc(size); \
        if ((ptr) == NULL) \
        { \
            MY_LOG_E("[MEM] Alloc failed: %d bytes [%s] at %s:%d", (size), (tag), __FILE__, __LINE__); \
            return (ret_val); \
        } \
    } while (0)

/* 安全内存释放宏：释放前检查NULL，释放后置NULL，防止Double Free */
#define MY_SAFE_FREE(ptr) \
    do { \
        if ((ptr) != NULL) \
        { \
            vPortFree((void *)(ptr)); \
            (ptr) = NULL; \
        } \
    } while (0)

/* 空指针检查宏（返回版本）：为空时输出错误日志并返回 */
#define MY_CHECK_PTR_RETURN(ptr, ret_val) \
    do { \
        if ((ptr) == NULL) \
        { \
            MY_LOG_E("[PARAM] NULL pointer at %s:%d", __FILE__, __LINE__); \
            return (ret_val); \
        } \
    } while (0)

/* 空指针检查宏（void版本）：为空时输出错误日志并返回 */
#define MY_CHECK_PTR_RETURN_VOID(ptr) \
    do { \
        if ((ptr) == NULL) \
        { \
            MY_LOG_E("[PARAM] NULL pointer at %s:%d", __FILE__, __LINE__); \
            return; \
        } \
    } while (0)

/*********************************************************************
 * @brief   内存健康检查
 *********************************************************************/
void my_safe_memory_check_health(void);

/*********************************************************************
 * @brief   获取内存统计信息
 * @param   free_size       当前可用堆大小输出指针
 * @param   min_free_size   历史最小可用堆大小输出指针
 * @param   usage_percent   当前使用率输出指针（百分比）
 *********************************************************************/
void my_safe_memory_get_stats(size_t *free_size, size_t *min_free_size, uint8_t *usage_percent);

#ifdef __cplusplus
}
#endif

#endif /* __MY_SAFE_MEMORY_H */
