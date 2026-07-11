/*******************************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       lfs_config.h
**文件描述：       LittleFS文件系统配置定义
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.06.02
*******************************************************************************
** 功能描述：       1. 定义LittleFS在GD32F50x平台的配置参数
**                 2. 定义文件系统句柄和全局配置
**                 3. 提供移植层接口声明
*******************************************************************************/

#ifndef __LFS_CONFIG_H__
#define __LFS_CONFIG_H__

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "my_log.h"         /**< 引入我们的日志系统 */
#include "my_safe_memory.h" /**< 安全内存管理 */
#include "param_flash.h"    /**< FLASH驱动配置 */

/*******************************************************************************
 * LittleFS 平台适配（集中管理）
 *******************************************************************************
 * 利用 lfs_util.h 的 #ifndef 守卫机制，在此处重定义 LittleFS 内部宏，
 * 将其映射到我们的日志系统和断言处理，避免默认使用 printf/assert。
 *
 * 必须在 #include "lfs.h" 之前定义，确保 lfs_util.h 跳过默认实现。
 ******************************************************************************/

/******************************************************************************
 * 调试开关：1=开启LittleFS内部日志和断言，0=关闭（发布模式）
 * 切换此宏即可在调试/发布模式间切换，无需修改 Keil 工程配置
 *****************************************************************************/
#define LFS_LOG_ENABLE          1

#if LFS_LOG_ENABLE

/* === 调试模式：将LittleFS日志重定向到MY_LOG系统 === */

#define LFS_ERROR(fmt, ...)   MY_LOG_E(fmt, ##__VA_ARGS__)
#define LFS_WARN(fmt, ...)    MY_LOG_W(fmt, ##__VA_ARGS__)
#define LFS_DEBUG(fmt, ...)   MY_LOG_D(fmt, ##__VA_ARGS__)
#define LFS_TRACE(fmt, ...)   MY_LOG_I(fmt, ##__VA_ARGS__)

#define LFS_ASSERT(test) do { \
    if (!(test)) { \
        MY_LOG_E("[LFS ASSERT FAIL] %s:%d: %s", __FILE__, __LINE__, #test); \
        while (1) {} \
    } \
} while (0)

#else

/* === 发布模式：禁用所有LittleFS内部日志和断言 === */

#define LFS_TRACE(...)   ((void)0)
#define LFS_ERROR(...)   ((void)0)
#define LFS_WARN(...)    ((void)0)
#define LFS_DEBUG(...)   ((void)0)
#define LFS_ASSERT(test) ((void)0)

#endif /* LFS_LOG_ENABLE */

/*******************************************************************************
 * @brief   ARM EABI断言失败处理函数（替代标准库__aeabi_assert）
 * @param   expr  断言表达式字符串
 * @param   file  断言失败所在源文件名
 * @param   line  断言失败所在行号
 * @return  无返回（进入死循环）
 * @note    禁止lfs_util.h引入<assert.h>时，仍需提供此符号使
 *          lfs_mlist_isopen等内联函数可编译链接。
 *          触发后通过日志输出失败位置，随后进入死循环等待调试。
 ******************************************************************************/
static inline void __aeabi_assert(const char *expr, const char *file, int line)
{
    MY_LOG_E("[ASSERT FAIL] %s:%d: %s", file, line, expr);
    while (1) {}
}

/*******************************************************************************
 * @brief   LittleFS安全内存分配（替代标准malloc）
 * @param   size  需要分配的字节数
 * @return  成功返回分配内存指针，失败返回NULL
 * @note    内部调用pvPortMalloc，分配失败时打印错误日志。
 *          通过宏LFS_MALLOC重定向给LittleFS使用。
 ******************************************************************************/
static inline void *lfs_safe_malloc(size_t size)
{
    void *p = pvPortMalloc(size);
    if (p == NULL)
    {
        MY_LOG_E("[LFS] Alloc failed: %d bytes", (int)size);
    }
    return p;
}

/*******************************************************************************
 * @brief   LittleFS安全内存释放（替代标准free）
 * @param   p  指向待释放指针的二级指针
 * @return  无
 * @note    释放后自动将原指针置NULL，防止悬空指针。
 *          通过宏LFS_FREE重定向给LittleFS使用。
 ******************************************************************************/
static inline void lfs_safe_free(void **p)
{
    if (p != NULL && *p != NULL)
    {
        vPortFree(*p);
        *p = NULL;
    }
}

#define LFS_MALLOC(size)    lfs_safe_malloc(size)
#define LFS_FREE(p)         lfs_safe_free((void **)&(p))

/*******************************************************************************
 * LittleFS 核心配置
 ******************************************************************************/

/* 必须在include lfs.h之前定义，以启用线程安全支持 */
#define LFS_THREADSAFE          1               /**< 启用线程安全（FreeRTOS多任务访问） */
#define LFS_ENABLE_REOPEN       1               /**< 允许重新打开文件 */

/* 在lfs.h之前定义，覆盖默认值（避免宏重定义警告） */
#define LFS_NAME_MAX            (64U)           /**< 文件名最大长度（默认255，64足够） */
#define LFS_FILE_MAX            (4096U)         /**< 文件最大大小4KB（小系统参数存储，单文件不超过4KB） */
#define LFS_ATTR_MAX            (64U)           /**< 自定义属性最大大小（默认1022，64足够） */

#include "lfs.h"

/*******************************************************************************
 * LittleFS配置参数
 ******************************************************************************/

/*******************************************************************************
 * LittleFS FLASH配置（引用param_flash.h的FLASH驱动配置）
 *******************************************************************************
 * 注意：FLASH地址、扇区大小、扇区数量由param_flash.h统一管理
 *      修改PARAM_DEBUG_MODE即可切换调试/正式阶段
 *
 * 调试阶段：0x080C7000~0x080FAFFF (208KB, factory_storage之前)
 * 正式阶段：0x0800C000~0x0803FFFF (208KB, Bank0, 前80KB零等待)
 ******************************************************************************/

/* FLASH物理参数（与param_flash.h保持一致） */
#define LFS_FLASH_BASE_ADDR     PARAM_PARTITION_SETTING_BASE       /**< FLASH起始地址 */
#define LFS_FLASH_SIZE          PARAM_PARTITION_SETTING_SIZE        /**< FLASH大小 */
#define LFS_BLOCK_SIZE          PARAM_PARTITION_SETTING_SECTOR_SIZE /**< 扇区大小（调试4KB/正式2KB） */
#define LFS_BLOCK_COUNT         PARAM_PARTITION_SETTING_SECTOR_COUNT/**< 扇区数量（调试52/正式104） */

/* LittleFS性能参数 */
#define LFS_READ_SIZE           (4U)            /**< 4字节读（FLASH最小读取单位） */
#define LFS_PROG_SIZE           (4U)            /**< 4字节写（FLASH最小编程单位） */
#define LFS_BLOCK_CYCLES        (1024U)         /**< 1024次擦写后磨损均衡轮换 */
#define LFS_CACHE_SIZE          (256U)          /**< 256字节缓存（平衡性能和RAM占用） */
#define LFS_LOOKAHEAD_SIZE      (32U)           /**< 32字节预读位图（可管理256个块，实际52/104个块） */

/*******************************************************************************
 * 文件系统句柄
 ******************************************************************************/

/**
 * @brief   LittleFS文件系统实例
 */
typedef struct
{
    lfs_t                   lfs;            /**< LittleFS核心实例 */
    struct lfs_config       cfg;            /**< LittleFS配置 */
    bool                    mounted;        /**< 挂载状态 */

    /* 静态缓冲区（避免动态分配内存，防止LFS_ERR_NOMEM） */
    uint8_t                 read_buffer[LFS_CACHE_SIZE];           /**< 读缓存 */
    uint8_t                 prog_buffer[LFS_CACHE_SIZE];           /**< 写缓存 */
    uint8_t                 lookahead_buffer[LFS_LOOKAHEAD_SIZE];  /**< 预读位图 */
} lfs_handle_t;

/**
 * @brief   全局LittleFS句柄
 */
extern lfs_handle_t g_lfs_handle;

/*******************************************************************************
 * 移植层API
 ******************************************************************************/

/*******************************************************************************
 * @brief   初始化LittleFS移植层
 * @return  0: 成功，<0: 错误码（FLASH初始化失败或互斥锁创建失败）
 * @note    执行以下操作：
 *          1. 初始化底层FLASH驱动（param_flash_init）
 *          2. 创建FreeRTOS互斥锁（LFS_THREADSAFE开启时）
 *          3. 填充lfs_config结构体：注册read/prog/erase/sync回调
 *          4. 配置block_size、block_count、cache_size等参数
 *          5. 绑定静态缓冲区（read_buffer/prog_buffer/lookahead_buffer）
 *          调用顺序：lfs_port_init -> lfs_port_mount
 ******************************************************************************/
int lfs_port_init(void);

/*******************************************************************************
 * @brief   反初始化LittleFS移植层
 * @return  0: 成功
 * @note    执行以下操作：
 *          1. 若已挂载则自动卸载（lfs_port_unmount）
 *          2. 删除FreeRTOS互斥锁
 *          3. 反初始化底层FLASH驱动（param_flash_deinit）
 *          4. 清空g_lfs_handle全局结构体
 ******************************************************************************/
int lfs_port_deinit(void);

/*******************************************************************************
 * @brief   挂载LittleFS文件系统
 * @return  0: 成功，<0: 错误码（LFS_ERR_xxx）
 * @note    执行以下操作：
 *          1. 若已挂载则直接返回成功（幂等调用）
 *          2. 调用lfs_mount尝试挂载
 *          3. 若返回LFS_ERR_CORRUPT（首次使用或损坏），返回错误由上层处理
 *          调用前提：必须先调用lfs_port_init完成初始化
 ******************************************************************************/
int lfs_port_mount(void);

/*******************************************************************************
 * @brief   格式化LittleFS文件系统
 * @return  0: 成功，<0: 错误码
 * @note    格式化将清除setting_storage分区中所有数据！
 *          调用场景：
 *          1. 首次使用（lfs_port_mount返回LFS_ERR_CORRUPT后）
 *          2. 恢复出厂设置（主动清除所有参数）
 *          格式化后需重新调用lfs_port_mount()完成挂载。
 ******************************************************************************/
int lfs_port_format(void);

/*******************************************************************************
 * @brief   卸载LittleFS文件系统
 * @return  0: 成功，<0: 错误码
 * @note    若未挂载则直接返回成功（幂等调用）。
 *          卸载后文件系统缓存数据会flush到FLASH，不会丢失。
 ******************************************************************************/
int lfs_port_unmount(void);

#endif /* __LFS_CONFIG_H__ */
