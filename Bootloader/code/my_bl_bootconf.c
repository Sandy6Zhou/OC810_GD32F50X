/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       my_bl_bootconf.c
**文件描述：       Bootloader启动配置管理实现文件
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.06.22
*********************************************************************
** 功能描述：       1. bootconf读写实现
**                 2. CRC16校验与magic验证
*********************************************************************/

#include "my_bl.h"

/*********************************************************************
 * 内部函数实现
 *********************************************************************/

/*********************************************************************
 * @brief   读取 bootconf（含 magic 和 checksum 校验）
 * @param   bconf  输出 bootconf 结构指针
 * @return  BL_OK 成功，BL_ERR_CRC 校验失败，BL_ERR_INVALID 参数无效
 *********************************************************************/
int my_bl_bootconf_read(my_bl_bootconf_t *bconf)
{
    my_bl_bootconf_t tmp;
    uint16_t crc_calc;

    if ((const void *)bconf == 0U)
    {
        return BL_ERR_INVALID;
    }

    /* 从FLASH读取bootconf */
    my_bl_flash_read(BL_FLASH_BOOTCONF_BASE, (uint8_t *)&tmp, sizeof(tmp));

    /* 校验magic */
    if (tmp.magic != BL_BOOTCONF_MAGIC)
    {
        return BL_ERR_MAGIC;
    }

    /* 校验checksum（排除checksum字段本身） */
    crc_calc = my_bl_crc16_table((uint8_t *)&tmp,
                                 (uint32_t)(sizeof(tmp) - sizeof(uint32_t)));

    if (crc_calc != tmp.checksum)
    {
        return BL_ERR_CRC;
    }

    memcpy(bconf, &tmp, sizeof(tmp));

    return BL_OK;
}

/*********************************************************************
 * @brief   写入 bootconf（自动计算 checksum）
 * @param   bconf  bootconf 结构指针
 * @return  BL_OK 成功，BL_ERR_FLASH 写入失败，BL_ERR_INVALID 参数无效
 * @note    擦写整个 4KB 页
 *********************************************************************/
int my_bl_bootconf_write(const my_bl_bootconf_t *bconf)
{
    int ret;
    my_bl_bootconf_t tmp;

    if ((const void *)bconf == 0U)
    {
        return BL_ERR_INVALID;
    }

    memcpy(&tmp, bconf, sizeof(tmp));

    /* 计算checksum */
    tmp.checksum = (uint32_t)my_bl_crc16_table((uint8_t *)&tmp,
                                               (uint32_t)(sizeof(tmp) - sizeof(uint32_t)));

    /* 擦除bootconf页 */
    ret = my_bl_flash_erase(BL_FLASH_BOOTCONF_BASE, BL_FLASH_BOOTCONF_SIZE);

    if (ret != BL_OK)
    {
        return ret;
    }

    /* 写入bootconf（结构体大小需4字节对齐） */
    ret = my_bl_flash_program(BL_FLASH_BOOTCONF_BASE,
                               (const uint8_t *)&tmp,
                               sizeof(tmp));

    return ret;
}

/*********************************************************************
 * @brief   初始化 bootconf 为默认值
 * @param   bconf  bootconf 结构指针
 * @return  None
 *********************************************************************/
void my_bl_bootconf_init_default(my_bl_bootconf_t *bconf)
{
    if ((const void *)bconf == 0U)
    {
        return;
    }

    memset(bconf, 0, sizeof(*bconf));

    bconf->magic = BL_BOOTCONF_MAGIC;

    /* 填充Bootloader版本信息 */
    snprintf(bconf->version, BL_BOOTCONF_VERSION_MAX_LEN, "v%s %s %s",
             BL_VERSION_STRING, BL_BUILD_DATE, BL_BUILD_TIME);

    bconf->ota_flag = BL_OTA_FLAG_NONE;
    bconf->last_boot_status = BL_BOOT_STATUS_NORMAL;
}
