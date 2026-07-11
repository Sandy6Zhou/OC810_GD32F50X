/*******************************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       param_flash.c
**文件描述：       FLASH全分区读写擦除驱动实现
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.06.02
*******************************************************************************
** 功能描述：       1. 封装GD32F50x FMC底层操作（双Bank标志管理、页擦除/字编程）
**                 2. 提供全分区读/编程/擦除统一接口（自动识别Bank页大小）
**                 3. 地址合法性校验，防止误操作其他分区
*******************************************************************************/

#include "param_flash.h"
#include "gd32f50x.h"
#include <string.h>

/*******************************************************************************
 * 内部变量
 ******************************************************************************/

static bool s_flash_initialized = false;

/*******************************************************************************
 * 内部辅助函数
 ******************************************************************************/

/*******************************************************************************
 * @brief   检查地址对齐
 * @param   addr        地址
 * @param   alignment   对齐字节数
 * @return  true: 对齐，false: 未对齐
 *******************************************************************************/
static bool flash_check_alignment(uint32_t addr, uint32_t alignment)
{
    return (addr % alignment) == 0;
}

/*******************************************************************************
 * @brief   根据地址获取所在Bank的硬件页大小
 * @param   addr    FLASH地址
 * @return  页大小(字节): Bank0=2KB, Bank1=4KB
 *******************************************************************************/
static uint32_t flash_get_page_size(uint32_t addr)
{
    return (addr >= PARAM_FLASH_BANK0_END_ADDR) ?
           PARAM_FLASH_BANK1_PAGE_SIZE : PARAM_FLASH_BANK0_PAGE_SIZE;
}

/*******************************************************************************
 * @brief   清除双Bank所有FMC标志（END + WPERR + PGERR）
 * @note    在FLASH操作前调用，无条件清除两个Bank的标志位。
 *          支持跨Bank操作（如app分区跨越0x08080000边界）。
 *          额外清除3个寄存器的开销（纳秒级）相比FLASH操作（毫秒级）可忽略。
 *******************************************************************************/
static void flash_clear_all_flags(void)
{
    /* Bank0 */
    fmc_flag_clear(FMC_FLAG_BANK0_END);
    fmc_flag_clear(FMC_FLAG_BANK0_WPERR);
    fmc_flag_clear(FMC_FLAG_BANK0_PGERR);

    /* Bank1 */
    fmc_flag_clear(FMC_FLAG_BANK1_END);
    fmc_flag_clear(FMC_FLAG_BANK1_WPERR);
    fmc_flag_clear(FMC_FLAG_BANK1_PGERR);
}

/*******************************************************************************
 * @brief   清除双Bank错误标志（WPERR + PGERR）
 * @note    在FLASH操作失败时调用，无条件清除两个Bank的错误标志。
 *          操作即将终止返回，额外清除开销可忽略。
 *******************************************************************************/
static void flash_clear_error_flags(void)
{
    /* Bank0 */
    fmc_flag_clear(FMC_FLAG_BANK0_WPERR);
    fmc_flag_clear(FMC_FLAG_BANK0_PGERR);

    /* Bank1 */
    fmc_flag_clear(FMC_FLAG_BANK1_WPERR);
    fmc_flag_clear(FMC_FLAG_BANK1_PGERR);
}

/*******************************************************************************
 * @brief   清除双Bank操作完成标志（END）
 * @note    在每次字编程/页擦除操作成功后调用。
 *          无条件清除两个Bank的END标志，正确处理跨Bank边界的操作。
 *******************************************************************************/
static void flash_clear_end_flag(void)
{
    fmc_flag_clear(FMC_FLAG_BANK0_END);
    fmc_flag_clear(FMC_FLAG_BANK1_END);
}

/*******************************************************************************
 * 公开API实现
 ******************************************************************************/

/*******************************************************************************
 * @brief   初始化FLASH驱动
 * @return  PARAM_FLASH_OK: 成功，其他: 失败
 * @note    初始化FMC控制器，检查分区有效性
 *******************************************************************************/
int param_flash_init(void)
{
    if (s_flash_initialized)
    {
        return PARAM_FLASH_OK;
    }

    /* 检查分区地址合法性 */
    if (!flash_check_alignment(PARAM_PARTITION_SETTING_BASE, PARAM_PARTITION_SETTING_SECTOR_SIZE))
    {
        return PARAM_FLASH_ERR_ADDR;
    }

    /* GD32F50x FMC已在系统启动时初始化，此处无需额外操作 */
    s_flash_initialized = true;

    return PARAM_FLASH_OK;
}

/*******************************************************************************
 * @brief   反初始化FLASH驱动
 * @return  PARAM_FLASH_OK: 成功，其他: 失败
 *******************************************************************************/
int param_flash_deinit(void)
{
    s_flash_initialized = false;

    return PARAM_FLASH_OK;
}

/*******************************************************************************
 * @brief   读取FLASH数据
 * @param   addr    绝对地址（必须在某个可写分区内）
 * @param   buf     输出缓冲区
 * @param   size    读取字节数
 * @return  PARAM_FLASH_OK: 成功，其他: 失败
 * @note    直接内存拷贝，无需FMC解锁，支持所有可写分区
 *******************************************************************************/
int param_flash_read(uint32_t addr, void *buf, uint32_t size)
{
    /* 参数检查 */
    if (buf == NULL || size == 0)
    {
        return PARAM_FLASH_ERR_SIZE;
    }

    /* 地址合法性检查 */
    if (!param_flash_check_addr(addr, size))
    {
        return PARAM_FLASH_ERR_ADDR;
    }

    /* 直接内存拷贝（FLASH可读如同RAM） */
    memcpy(buf, (const void *)addr, size);

    return PARAM_FLASH_OK;
}

/*******************************************************************************
 * @brief   编程FLASH数据（按字写入）
 * @param   addr    绝对地址（必须在某个可写分区内）
 * @param   buf     数据缓冲区
 * @param   size    写入字节数（必须是4的倍数）
 * @return  PARAM_FLASH_OK: 成功，其他: 失败
 * @note    写入前必须先擦除，addr必须4字节对齐，支持所有可写分区
 *******************************************************************************/
int param_flash_program(uint32_t addr, const void *buf, uint32_t size)
{
    uint32_t i;
    fmc_state_enum state;

    /* 参数检查 */
    if (buf == NULL || size == 0)
    {
        return PARAM_FLASH_ERR_SIZE;
    }

    /* 地址合法性检查 */
    if (!param_flash_check_addr(addr, size))
    {
        return PARAM_FLASH_ERR_ADDR;
    }

    /* 检查4字节对齐 */
    if (!flash_check_alignment(addr, 4) || !flash_check_alignment(size, 4))
    {
        return PARAM_FLASH_ERR_ADDR;
    }

    /* 解锁FMC */
    fmc_unlock();

    /* 清除双Bank所有待处理标志（兼容跨Bank操作） */
    flash_clear_all_flags();

    /* 按字编程 */
    for (i = 0; i < size; i += 4)
    {
        state = fmc_word_program(addr + i, *(const uint32_t *)((const uint8_t *)buf + i));
        if (state != FMC_READY)
        {
            /* 清除错误标志 */
            flash_clear_error_flags();
            fmc_lock();
            return PARAM_FLASH_ERR_PROGRAM;
        }

        /* 清除操作完成标志 */
        flash_clear_end_flag();
    }

    /* 锁定FMC */
    fmc_lock();

    return PARAM_FLASH_OK;
}

/*******************************************************************************
 * @brief   擦除FLASH扇区
 * @param   addr    扇区起始地址（必须在某个可写分区内，且页对齐）
 * @param   size    擦除大小（必须是所在Bank页大小的整数倍）
 * @return  PARAM_FLASH_OK: 成功，其他: 失败
 * @note    支持setting_storage/mcu_secondary/factory_storage/bootconf全部分区
 *          Bank0按2KB页擦除，Bank1按4KB页擦除，自动识别
 *******************************************************************************/
int param_flash_erase(uint32_t addr, uint32_t size)
{
    fmc_state_enum state;
    uint32_t current_addr;
    uint32_t page_size;

    /* 参数检查 */
    if (size == 0)
    {
        return PARAM_FLASH_ERR_SIZE;
    }

    /* 地址合法性检查（必须在某个可写分区内） */
    if (!param_flash_check_addr(addr, size))
    {
        return PARAM_FLASH_ERR_ADDR;
    }

    /* 根据地址所在Bank获取硬件页大小 */
    page_size = flash_get_page_size(addr);

    /* 检查页对齐 */
    if (!flash_check_alignment(addr, page_size) ||
        !flash_check_alignment(size, page_size))
    {
        return PARAM_FLASH_ERR_ADDR;
    }

    /* 解锁FMC */
    fmc_unlock();

    /* 清除双Bank所有待处理标志（兼容跨Bank操作） */
    flash_clear_all_flags();

    /* 逐页擦除（根据每个页的Bank动态确定页大小） */
    current_addr = addr;
    while (current_addr < (addr + size))
    {
        page_size = flash_get_page_size(current_addr);

        state = fmc_page_erase(current_addr);
        if (state != FMC_READY)
        {
            /* 清除错误标志 */
            flash_clear_error_flags();
            fmc_lock();
            return PARAM_FLASH_ERR_ERASE;
        }

        /* 清除操作完成标志 */
        flash_clear_end_flag();

        current_addr += page_size;
    }

    /* 锁定FMC */
    fmc_lock();

    return PARAM_FLASH_OK;
}

/*******************************************************************************
 * @brief   检查地址范围是否在任意可写分区内
 * @param   addr    起始地址
 * @param   size    数据大小
 * @return  true: 合法，false: 非法
 * @note    校验地址范围必须完全落在以下某个分区内：
 *          setting_storage / mcu_secondary / factory_storage / bootconf
 *          不允许操作跨越分区边界或落在未定义区域(mcuboot/app由各自模块管理)
 *******************************************************************************/
bool param_flash_check_addr(uint32_t addr, uint32_t size)
{
    uint32_t end_addr;
    uint32_t part_start, part_end;
    uint8_t i;

    /* 可写分区表: {起始地址, 大小} */
    static const struct {
        uint32_t base;
        uint32_t size;
    } s_writable_partitions[] = {
        { PARAM_PARTITION_SETTING_BASE, PARAM_PARTITION_SETTING_SIZE },
        { PARAM_PARTITION_MCU_SEC_BASE, PARAM_PARTITION_MCU_SEC_SIZE },
        { PARAM_PARTITION_FACTORY_BASE, PARAM_PARTITION_FACTORY_SIZE },
        { PARAM_PARTITION_BOOTCONF_BASE, PARAM_PARTITION_BOOTCONF_SIZE },
    };
    #define WRITABLE_PARTITION_COUNT \
        (sizeof(s_writable_partitions) / sizeof(s_writable_partitions[0]))

    /* 防止整数溢出 */
    if (size > 0 && addr > (UINT32_MAX - size))
    {
        return false;
    }

    end_addr = addr + size;

    /* 检查是否完全落在某个可写分区内 */
    for (i = 0; i < WRITABLE_PARTITION_COUNT; i++)
    {
        part_start = s_writable_partitions[i].base;
        part_end = part_start + s_writable_partitions[i].size;

        if ((addr >= part_start) && (end_addr <= part_end))
        {
            return true;
        }
    }

    return false;
}
