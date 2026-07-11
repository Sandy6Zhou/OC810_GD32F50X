/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       my_bl_bootconf.h
**文件描述：       Bootloader启动配置管理头文件
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.06.22
*********************************************************************
** 功能描述：       bootconf结构定义与读写接口声明
*********************************************************************/

#ifndef __MY_BL_BOOTCONF_H__
#define __MY_BL_BOOTCONF_H__

#include <stdint.h>

/*********************************************************************
 * bootconf 魔数与标志定义
 *********************************************************************/
#define BL_BOOTCONF_MAGIC           0x42434F4EU     /* "BCON" */

/* OTA标志（使用明显不同的值，便于调试时快速识别） */
#define BL_OTA_FLAG_NONE            (0x00U)   /* 无OTA操作 */
#define BL_OTA_FLAG_PENDING         (0x01U)   /* OTA待处理（固件已写入OTA分区，等待升级） */
#define BL_OTA_FLAG_FAILED          (0x02U)   /* OTA失败（校验错误或擦写失败） */

/* 启动状态（使用明显不同的值） */
#define BL_BOOT_STATUS_NORMAL       (0x00U)   /* 正常启动 */
#define BL_BOOT_STATUS_UPGRADING    (0x01U)   /* 升级中（已擦除APP，正在复制） */
#define BL_BOOT_STATUS_UPGRADED     (0x02U)   /* 升级完成（首次启动新固件） */

/*********************************************************************
 * bootconf 数据结构（76字节）
 * magic(4) + ota_flag(4) + last_boot_status(4) + file_id(4) + file_md5(16)
 * + file_size(4) + file_crc16(2) + reserved(2) + version[32](32) + checksum(4) = 76
 *********************************************************************/
#define BL_BOOTCONF_VERSION_MAX_LEN     (32U)   /**< Bootloader版本信息最大长度 */

typedef struct
{
    uint32_t magic;                     /**< 固定 0x42434F4E */
    uint32_t ota_flag;                  /**< OTA标志 */
    uint32_t last_boot_status;          /**< 上次启动状态 */

    uint8_t  file_id[4];                /**< 固件文件ID */
    uint8_t  file_md5[16];              /**< 固件MD5校验值 */
    uint32_t file_size;                 /**< 固件大小（字节） */
    uint16_t file_crc16;                /**< 固件CRC16校验值 */
    uint16_t reserved;                  /**< 保留字段（4字节对齐） */

    char     version[BL_BOOTCONF_VERSION_MAX_LEN];  /**< Bootloader版本信息字符串 */
    uint32_t checksum;                  /**< bootconf自身CRC32 */
} my_bl_bootconf_t;

/*********************************************************************
 * @brief   读取 bootconf（含 magic 和 checksum 校验）
 * @param   bconf  输出 bootconf 结构指针
 * @return  BL_OK 成功，BL_ERR_CRC 校验失败，BL_ERR_INVALID 参数无效
 *********************************************************************/
int my_bl_bootconf_read(my_bl_bootconf_t *bconf);

/*********************************************************************
 * @brief   写入 bootconf（自动计算 checksum）
 * @param   bconf  bootconf 结构指针
 * @return  BL_OK 成功，BL_ERR_FLASH 写入失败，BL_ERR_INVALID 参数无效
 * @note    擦写整个 4KB 页
 *********************************************************************/
int my_bl_bootconf_write(const my_bl_bootconf_t *bconf);

/*********************************************************************
 * @brief   初始化 bootconf 为默认值
 * @param   bconf  bootconf 结构指针
 * @return  None
 *********************************************************************/
void my_bl_bootconf_init_default(my_bl_bootconf_t *bconf);

#endif /* __MY_BL_BOOTCONF_H__ */
