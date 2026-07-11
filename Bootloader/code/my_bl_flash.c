/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       my_bl_flash.c
**文件描述：       Bootloader FLASH操作实现文件
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.06.22
*********************************************************************
** 功能描述：       1. 封装GD32 FMC驱动实现FLASH擦除/编程/读取
**                 2. 编程后逐字验证写入正确性
*********************************************************************/

#include "my_bl.h"

/*********************************************************************
 * 内部函数实现
 *********************************************************************/

/*********************************************************************
 * @brief   擦除FLASH页
 * @param   addr: 数据起始地址
 * @param   size: 数据大小
 * @return  int: 错误码
 *********************************************************************/
int my_bl_flash_erase(uint32_t addr, uint32_t size)
{
    uint32_t page_count;
    uint32_t i;

    if ((size == 0U) || (addr % BL_FLASH_PAGE_SIZE != 0U))
    {
        return BL_ERR_INVALID;
    }

    page_count = (size + BL_FLASH_PAGE_SIZE - 1U) / BL_FLASH_PAGE_SIZE;

    fmc_unlock();

    for (i = 0U; i < page_count; i++)
    {
        fmc_state_enum state;

        /* 清除标志 */
        fmc_flag_clear(FMC_FLAG_BANK0_END);
        fmc_flag_clear(FMC_FLAG_BANK0_WPERR);
        fmc_flag_clear(FMC_FLAG_BANK0_PGERR);

        /* 擦除页 */
        state = fmc_page_erase(addr + i * BL_FLASH_PAGE_SIZE);

        if (state != FMC_READY)
        {
            fmc_lock();
            return BL_ERR_FLASH;
        }

        /* 清除操作完成标志 */
        fmc_flag_clear(FMC_FLAG_BANK0_END);
    }

    fmc_lock();

    return BL_OK;
}

/*********************************************************************
 * @brief   编程FLASH数据
 * @param   addr: 数据起始地址
 * @param   data: 数据缓冲区指针
 * @param   size: 数据大小
 * @return  int: 错误码
 *********************************************************************/
int my_bl_flash_program(uint32_t addr, const uint8_t *data, uint32_t size)
{
    uint32_t i;
    uint32_t word;

    if (((const void *)data == 0U) || (size == 0U) || (size % 4U != 0U))
    {
        return BL_ERR_INVALID;
    }

    fmc_unlock();

    for (i = 0U; i < size; i += 4U)
    {
        fmc_state_enum state;

        /* 清除标志 */
        fmc_flag_clear(FMC_FLAG_BANK0_END);
        fmc_flag_clear(FMC_FLAG_BANK0_WPERR);
        fmc_flag_clear(FMC_FLAG_BANK0_PGERR);

        /* 组装字数据（小端序） */
        word = (uint32_t)data[i] |
               ((uint32_t)data[i + 1U] << 8U) |
               ((uint32_t)data[i + 2U] << 16U) |
               ((uint32_t)data[i + 3U] << 24U);

        /* 编程 */
        state = fmc_word_program(addr + i, word);

        if (state != FMC_READY)
        {
            fmc_lock();
            return BL_ERR_FLASH;
        }

        /* 验证写入 */
        if (*(volatile uint32_t *)(addr + i) != word)
        {
            fmc_lock();
            return BL_ERR_VERIFY;
        }

        fmc_flag_clear(FMC_FLAG_BANK0_END);
    }

    fmc_lock();

    return BL_OK;
}

/*********************************************************************
 * @brief   读取FLASH数据
 * @param   addr: 数据起始地址
 * @param   buf: 数据缓冲区指针
 * @param   size: 数据大小
 * @return  int: 错误码
 *********************************************************************/
int my_bl_flash_read(uint32_t addr, uint8_t *buf, uint32_t size)
{
    if (((const void *)buf == 0U) || (size == 0U))
    {
        return BL_ERR_INVALID;
    }

    memcpy(buf, (const void *)addr, size);

    return BL_OK;
}
