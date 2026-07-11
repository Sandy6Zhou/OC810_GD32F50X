/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       my_safe_memory.c
**文件描述：       安全内存管理模块实现文件
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.04.17
*********************************************************************
** 功能描述：       1. 实现内存健康检查功能
**                 2. 实现内存统计信息查询功能
*********************************************************************/

#include "my_safe_memory.h"

/* 内存告警阈值（字节） */
#define MY_SAFE_MEM_CRITICAL_THRESHOLD    (1024U)   /* 严重阈值 */
#define MY_SAFE_MEM_WARN_THRESHOLD        (2048U)   /* 警告阈值 */
#define MY_SAFE_MEM_MIN_THRESHOLD         (512U)    /* 历史最小值告警 */

/*********************************************************************
 * @brief   内存健康检查
 * @note    定期检查堆使用情况，低内存时输出告警日志
 *********************************************************************/
void my_safe_memory_check_health(void)
{
    size_t freeHeap = xPortGetFreeHeapSize();
    size_t minHeap = xPortGetMinimumEverFreeHeapSize();
    size_t totalHeap = configTOTAL_HEAP_SIZE;

    uint8_t usage = (uint8_t)((totalHeap - freeHeap) * 100U / totalHeap);

    if (freeHeap < MY_SAFE_MEM_CRITICAL_THRESHOLD)
    {
        MY_LOG_E("[MEM] Low heap: %u bytes (%u%% used)", (unsigned)freeHeap, (unsigned)usage);
    }
    else if (freeHeap < MY_SAFE_MEM_WARN_THRESHOLD)
    {
        MY_LOG_W("[MEM] Heap warning: %u bytes (%u%% used)", (unsigned)freeHeap, (unsigned)usage);
    }

    if (minHeap < MY_SAFE_MEM_MIN_THRESHOLD)
    {
        MY_LOG_E("[MEM] Critical min heap: %u bytes", (unsigned)minHeap);
    }
}

/*********************************************************************
 * @brief   获取内存统计信息
 * @param   free_size       当前可用堆大小输出指针
 * @param   min_free_size   历史最小可用堆大小输出指针
 * @param   usage_percent   当前使用率输出指针（百分比）
 * @note    所有参数均为可选，传NULL表示不获取该项
 *********************************************************************/
void my_safe_memory_get_stats(size_t *free_size, size_t *min_free_size, uint8_t *usage_percent)
{
    if (free_size != NULL)
    {
        *free_size = xPortGetFreeHeapSize();
    }

    if (min_free_size != NULL)
    {
        *min_free_size = xPortGetMinimumEverFreeHeapSize();
    }

    if (usage_percent != NULL)
    {
        size_t freeHeap = xPortGetFreeHeapSize();
        *usage_percent = (uint8_t)((configTOTAL_HEAP_SIZE - freeHeap) * 100U / configTOTAL_HEAP_SIZE);
    }
}
