/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       my_bl_ota.c
**文件描述：       Bootloader OTA升级实现文件
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.06.22
*********************************************************************
**功能描述：       1. OTA固件CRC32和MD5校验
**                 2. APP区擦除与固件拷贝
**                 3. 拷贝后完整性验证（CRC32+MD5）
*********************************************************************/

#include "my_bl.h"

/* OTA拷贝缓冲区 */
static uint8_t sChunkBuf[BL_OTA_CHUNK_SIZE];

/*********************************************************************
 * 函数实现
 *********************************************************************/

 /*********************************************************************
 * @brief   OTA升级
 * @param   bconf: Bootloader配置结构体指针
 * @return  int: 错误码
 *********************************************************************/
int my_bl_ota_upgrade(my_bl_bootconf_t *bconf)
{
    int ret;
    uint32_t remaining;
    uint32_t src_addr;
    uint32_t dst_addr;
    uint32_t chunk;
    uint16_t crc_src;
    uint16_t crc_dst;
    uint8_t md5_src[16];

    if (bconf == (void *)0U)
    {
        return BL_ERR_INVALID;
    }

    if ((bconf->file_size == 0U) ||
        (bconf->file_size > BL_FLASH_OTA_SIZE))
    {
        MY_LOG_E("OTA: invalid file size %u", bconf->file_size);
        return BL_ERR_INVALID;
    }

    /* 1. 校验OTA源固件CRC16 */
    MY_LOG_I("OTA: verifying source firmware CRC16...");
    crc_src = my_bl_crc16_table((const uint8_t *)BL_FLASH_OTA_BASE, bconf->file_size);

    if (crc_src != bconf->file_crc16)
    {
        MY_LOG_E("OTA: source CRC16 mismatch 0x%04X != 0x%04X", crc_src, bconf->file_crc16);
        return BL_ERR_CRC;
    }

    MY_LOG_I("OTA: source CRC16 OK");

    /* 2. 校验OTA源固件MD5 */
    MY_LOG_I("OTA: verifying source firmware MD5...");
    {
        my_bl_md5_ctx_t md5_ctx;
        uint8_t md5_src[16];

        my_bl_md5_init(&md5_ctx);
        my_bl_md5_update(&md5_ctx, (const uint8_t *)BL_FLASH_OTA_BASE, bconf->file_size);
        my_bl_md5_final(&md5_ctx, md5_src);

        if (memcmp(md5_src, bconf->file_md5, 16) != 0)
        {
            MY_LOG_E("OTA: source MD5 mismatch");
            return BL_ERR_CRC;
        }
    }

    MY_LOG_I("OTA: source MD5 OK");

    /* 3. 擦除APP区 */
    MY_LOG_I("OTA: erasing APP area (%u KB)...", BL_FLASH_APP_SIZE / 1024U);

    ret = my_bl_flash_erase(BL_FLASH_APP_BASE, BL_FLASH_APP_SIZE);

    if (ret != BL_OK)
    {
        MY_LOG_E("OTA: erase failed %d", ret);
        return ret;
    }

    /* 4. 拷贝固件 */
    MY_LOG_I("OTA: copying firmware...");

    remaining = bconf->file_size;
    src_addr = BL_FLASH_OTA_BASE;
    dst_addr = BL_FLASH_APP_BASE;

    while (remaining > 0U)
    {
        chunk = (remaining > BL_OTA_CHUNK_SIZE) ? BL_OTA_CHUNK_SIZE : remaining;

        /* 读取OTA区 */
        ret = my_bl_flash_read(src_addr, sChunkBuf, chunk);

        if (ret != BL_OK)
        {
            MY_LOG_E("OTA: read failed at 0x%08X", src_addr);
            return ret;
        }

        /* 写入APP区 */
        ret = my_bl_flash_program(dst_addr, sChunkBuf, chunk);

        if (ret != BL_OK)
        {
            MY_LOG_E("OTA: program failed at 0x%08X", dst_addr);
            return ret;
        }

        src_addr += chunk;
        dst_addr += chunk;
        remaining -= chunk;
    }

    MY_LOG_I("OTA: copy complete");

    /* 5. 校验拷贝结果 */
    MY_LOG_I("OTA: verifying copy...");
    crc_dst = my_bl_crc16_table((const uint8_t *)BL_FLASH_APP_BASE, bconf->file_size);

    if (crc_dst != bconf->file_crc16)
    {
        MY_LOG_E("OTA: copy CRC16 mismatch 0x%04X != 0x%04X", crc_dst, bconf->file_crc16);
        return BL_ERR_VERIFY;
    }

    MY_LOG_I("OTA: upgrade completed successfully");

    return BL_OK;
}
