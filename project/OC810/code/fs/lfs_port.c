/*******************************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       lfs_port.c
**文件描述：       LittleFS移植层实现（GD32F50x + FreeRTOS）
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.06.02
*******************************************************************************
** 功能描述：       1. 实现LittleFS需要的block device回调函数
**                 2. 适配GD32F50x FLASH驱动
**                 3. 实现FreeRTOS线程安全保护
*******************************************************************************/

#include "lfs_config.h"
#include "param_flash.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <string.h>

/*******************************************************************************
 * 全局变量
 ******************************************************************************/

lfs_handle_t g_lfs_handle = {0};

/*******************************************************************************
 * 内部变量
 ******************************************************************************/

/* 线程安全互斥锁 */
static SemaphoreHandle_t s_lfs_mutex = NULL;

/*******************************************************************************
 * LittleFS Block Device回调函数
 ******************************************************************************/

/*******************************************************************************
 * @brief   读取FLASH数据（LittleFS block device回调）
 * @param   c       LittleFS配置结构体指针
 * @param   block   块编号（从0开始）
 * @param   off     块内偏移（字节）
 * @param   buffer  数据读取缓冲区
 * @param   size    读取字节数
 * @return  0: 成功，<0: LFS_ERR_xxx错误码
 * @note    LittleFS内部调用，将逻辑块地址转换为物理FLASH地址后读取。
 *          包含block边界校验，越界时返回LFS_ERR_INVAL。
 *******************************************************************************/
static int lfs_block_read(const struct lfs_config *c, lfs_block_t block,
                          lfs_off_t off, void *buffer, lfs_size_t size)
{
    uint32_t addr;

    /* 边界检查 */
    if (block >= c->block_count)
    {
        return LFS_ERR_INVAL;
    }

    /* 计算绝对地址 */
    addr = LFS_FLASH_BASE_ADDR + (block * c->block_size) + off;

    /* 调用FLASH驱动读取 */
    return param_flash_read(addr, buffer, size);
}

/*******************************************************************************
 * @brief   编程FLASH数据（LittleFS block device回调）
 * @param   c       LittleFS配置结构体指针
 * @param   block   块编号（从0开始）
 * @param   off     块内偏移（字节）
 * @param   buffer  待写入数据缓冲区
 * @param   size    写入字节数
 * @return  0: 成功，<0: LFS_ERR_xxx错误码
 * @note    LittleFS内部调用，将逻辑块地址转换为物理FLASH地址后编程。
 *          写入前对应区域必须已擦除（由LittleFS保证擦除-写入顺序）。
 *******************************************************************************/
static int lfs_block_prog(const struct lfs_config *c, lfs_block_t block,
                          lfs_off_t off, const void *buffer, lfs_size_t size)
{
    uint32_t addr;

    /* 边界检查 */
    if (block >= c->block_count)
    {
        return LFS_ERR_INVAL;
    }

    /* 计算绝对地址 */
    addr = LFS_FLASH_BASE_ADDR + (block * c->block_size) + off;

    /* 调用FLASH驱动编程 */
    return param_flash_program(addr, buffer, size);
}

/*******************************************************************************
 * @brief   擦除FLASH扇区（LittleFS block device回调）
 * @param   c       LittleFS配置结构体指针
 * @param   block   块编号（从0开始）
 * @return  0: 成功，<0: LFS_ERR_xxx错误码
 * @note    按block_size大小擦除整个块。擦除后块内数据全为0xFF。
 *          LittleFS wear-leveling机制会控制擦除频率均衡磨损。
 *******************************************************************************/
static int lfs_block_erase(const struct lfs_config *c, lfs_block_t block)
{
    uint32_t addr;

    /* 边界检查 */
    if (block >= c->block_count)
    {
        return LFS_ERR_INVAL;
    }

    /* 计算绝对地址 */
    addr = LFS_FLASH_BASE_ADDR + (block * c->block_size);

    /* 调用FLASH驱动擦除 */
    return param_flash_erase(addr, c->block_size);
}

/*******************************************************************************
 * @brief   同步FLASH状态（LittleFS block device回调）
 * @param   c       LittleFS配置结构体指针（未使用）
 * @return  0: 始终成功
 * @note    GD32F50x内部FLASH为同步操作，无需额外同步。
 *          若使用外部SPI FLASH等异步存储则需在此处实现flush。
 *******************************************************************************/
static int lfs_block_sync(const struct lfs_config *c)
{
    (void)c;

    return 0;
}

#ifdef LFS_THREADSAFE
/*******************************************************************************
 * @brief   锁定FLASH设备（LittleFS block device回调）
 * @param   c       LittleFS配置结构体指针（未使用）
 * @return  0: 始终成功
 * @note    LFS_THREADSAFE开启时由LittleFS在每次文件系统操作前调用。
 *          通过FreeRTOS互斥锁实现多任务间FLASH访问互斥。
 *******************************************************************************/
static int lfs_block_lock(const struct lfs_config *c)
{
    (void)c;

    if (s_lfs_mutex != NULL)
    {
        xSemaphoreTake(s_lfs_mutex, portMAX_DELAY);
    }

    return 0;
}

/*******************************************************************************
 * @brief   解锁FLASH设备（LittleFS block device回调）
 * @param   c       LittleFS配置结构体指针（未使用）
 * @return  0: 始终成功
 * @note    LFS_THREADSAFE开启时由LittleFS在每次文件系统操作完成后调用。
 *          与lfs_block_lock成对使用，释放互斥锁。
 *******************************************************************************/
static int lfs_block_unlock(const struct lfs_config *c)
{
    (void)c;

    if (s_lfs_mutex != NULL)
    {
        xSemaphoreGive(s_lfs_mutex);
    }

    return 0;
}
#endif /* LFS_THREADSAFE */

/*******************************************************************************
 * 公开API实现
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
int lfs_port_init(void)
{
    int ret;

    /* 初始化FLASH驱动 */
    ret = param_flash_init();
    if (ret != PARAM_FLASH_OK)
    {
        return ret;
    }

    /* 创建互斥锁（线程安全） */
#ifdef LFS_THREADSAFE
    if (s_lfs_mutex == NULL)
    {
        s_lfs_mutex = xSemaphoreCreateMutex();
        if (s_lfs_mutex == NULL)
        {
            return -1;
        }
    }
#endif

    /* 配置LittleFS */
    g_lfs_handle.cfg.context = NULL;
    g_lfs_handle.cfg.read = lfs_block_read;
    g_lfs_handle.cfg.prog = lfs_block_prog;
    g_lfs_handle.cfg.erase = lfs_block_erase;
    g_lfs_handle.cfg.sync = lfs_block_sync;

#ifdef LFS_THREADSAFE
    g_lfs_handle.cfg.lock = lfs_block_lock;
    g_lfs_handle.cfg.unlock = lfs_block_unlock;
#endif

    g_lfs_handle.cfg.read_size = LFS_READ_SIZE;
    g_lfs_handle.cfg.prog_size = LFS_PROG_SIZE;
    g_lfs_handle.cfg.block_size = LFS_BLOCK_SIZE;
    g_lfs_handle.cfg.block_count = LFS_BLOCK_COUNT;
    g_lfs_handle.cfg.block_cycles = LFS_BLOCK_CYCLES;
    g_lfs_handle.cfg.cache_size = LFS_CACHE_SIZE;
    g_lfs_handle.cfg.lookahead_size = LFS_LOOKAHEAD_SIZE;
    g_lfs_handle.cfg.name_max = LFS_NAME_MAX;
    g_lfs_handle.cfg.file_max = LFS_FILE_MAX;
    g_lfs_handle.cfg.attr_max = LFS_ATTR_MAX;

    /* 配置静态缓冲区（避免动态分配内存） */
    g_lfs_handle.cfg.read_buffer = g_lfs_handle.read_buffer;
    g_lfs_handle.cfg.prog_buffer = g_lfs_handle.prog_buffer;
    g_lfs_handle.cfg.lookahead_buffer = g_lfs_handle.lookahead_buffer;

    g_lfs_handle.mounted = false;

    return 0;
}

/*******************************************************************************
 * @brief   反初始化LittleFS移植层
 * @return  0: 成功
 * @note    执行以下操作：
 *          1. 若已挂载则自动卸载（lfs_port_unmount）
 *          2. 删除FreeRTOS互斥锁
 *          3. 反初始化底层FLASH驱动（param_flash_deinit）
 *          4. 清空g_lfs_handle全局结构体
 ******************************************************************************/
int lfs_port_deinit(void)
{
    /* 卸载文件系统 */
    if (g_lfs_handle.mounted)
    {
        lfs_port_unmount();
    }

    /* 删除互斥锁 */
#ifdef LFS_THREADSAFE
    if (s_lfs_mutex != NULL)
    {
        vSemaphoreDelete(s_lfs_mutex);
        s_lfs_mutex = NULL;
    }
#endif

    /* 反初始化FLASH驱动 */
    param_flash_deinit();

    memset(&g_lfs_handle, 0, sizeof(g_lfs_handle));

    return 0;
}

/*******************************************************************************
 * @brief   挂载LittleFS文件系统
 * @return  0: 成功，<0: 错误码（LFS_ERR_xxx）
 * @note    执行以下操作：
 *          1. 若已挂载则直接返回成功（幂等调用）
 *          2. 调用lfs_mount尝试挂载
 *          3. 若返回LFS_ERR_CORRUPT（首次使用或损坏），返回错误由上层处理
 *          调用前提：必须先调用lfs_port_init完成初始化
 ******************************************************************************/
int lfs_port_mount(void)
{
    int ret;

    if (g_lfs_handle.mounted)
    {
        return 0;  /* 已经挂载 */
    }

    /* 尝试挂载文件系统 */
    ret = lfs_mount(&g_lfs_handle.lfs, &g_lfs_handle.cfg);
    if (ret == LFS_ERR_CORRUPT)
    {
        /* 文件系统损坏或首次使用，不自动格式化，返回错误由上层决定 */
        MY_LOG_E("[LFS] Filesystem corrupt or not formatted.");
        MY_LOG_E("[LFS] Call lfs_port_format() to format manually.");
        return LFS_ERR_CORRUPT;
    }
    else if (ret != 0)
    {
        return ret;
    }

    g_lfs_handle.mounted = true;

    return 0;
}

/*******************************************************************************
 * @brief   格式化LittleFS文件系统
 * @return  0: 成功，<0: 错误码
 * @note    格式化将清除setting_storage分区中所有数据！
 *          调用场景：
 *          1. 首次使用（lfs_port_mount返回LFS_ERR_CORRUPT后）
 *          2. 恢复出厂设置（主动清除所有参数）
 *          格式化后需重新调用lfs_port_mount()完成挂载。
 ******************************************************************************/
int lfs_port_format(void)
{
    int ret;

    /* 若已挂载，先卸载 */
    if (g_lfs_handle.mounted)
    {
        lfs_port_unmount();
    }

    /* 执行格式化 */
    ret = lfs_format(&g_lfs_handle.lfs, &g_lfs_handle.cfg);
    if (ret != 0)
    {
        MY_LOG_E("[LFS] Format failed: %d", ret);
        return ret;
    }

    MY_LOG_I("[LFS] Format OK");
    return 0;
}

/*******************************************************************************
 * @brief   卸载LittleFS文件系统
 * @return  0: 成功，<0: 错误码
 * @note    若未挂载则直接返回成功（幂等调用）。
 *          卸载后文件系统缓存数据会flush到FLASH，不会丢失。
 ******************************************************************************/
int lfs_port_unmount(void)
{
    int ret;

    if (!g_lfs_handle.mounted)
    {
        return 0;  /* 未挂载 */
    }

    ret = lfs_unmount(&g_lfs_handle.lfs);
    if (ret != 0)
    {
        return ret;
    }

    g_lfs_handle.mounted = false;

    return 0;
}
