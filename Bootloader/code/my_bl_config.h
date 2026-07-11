/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       my_bl_config.h
**文件描述：       Bootloader配置宏定义文件
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.06.22
*********************************************************************
** 功能描述：       1. FLASH分区地址定义
**                 2. UART与日志通道配置
**                 3. 错误码定义
*********************************************************************/

#ifndef __MY_BL_CONFIG_H__
#define __MY_BL_CONFIG_H__

/*********************************************************************
 * FLASH 分区地址定义（GD32F505VGT7, 1MB Main Flash）
 *********************************************************************/

/* Bootloader 分区 */
#define BL_FLASH_BOOT_BASE          0x08000000U
#define BL_FLASH_BOOT_SIZE          (48U * 1024U)

/* 参数存储分区（LittleFS） */
#define BL_FLASH_SETTING_BASE       0x0800C000U
#define BL_FLASH_SETTING_SIZE       (208U * 1024U)

/* APP 主应用分区 */
#define BL_FLASH_APP_BASE           0x08040000U
#define BL_FLASH_APP_SIZE           (376U * 1024U)

/* OTA 固件暂存分区 */
#define BL_FLASH_OTA_BASE           0x0809E000U
#define BL_FLASH_OTA_SIZE           (376U * 1024U)

/* 启动配置分区（移到Factory前面，便于批量烧录） */
#define BL_FLASH_BOOTCONF_BASE      0x080FC000U
#define BL_FLASH_BOOTCONF_SIZE      (4U * 1024U)

/* 工厂参数分区（放在最后，避免批量烧录时覆盖） */
#define BL_FLASH_FACTORY_BASE       0x080FD000U
#define BL_FLASH_FACTORY_SIZE       (12U * 1024U)

/*********************************************************************
 * FMC 配置
 *********************************************************************/
#define BL_FLASH_PAGE_SIZE          (2048U)

/*********************************************************************
 * 日志输出通道配置
 *********************************************************************/
#define BL_RTT_LOG_ENABLE           (1U)    /* 总开关（发布版本可关闭） */

/*********************************************************************
 * 日志级别配置
 *********************************************************************/
#define MY_BL_LOG_LEVEL_NONE      0   // 关闭所有日志
#define MY_BL_LOG_LEVEL_ERROR     1   // 错误级别
#define MY_BL_LOG_LEVEL_WARN      2   // 警告级别
#define MY_BL_LOG_LEVEL_INFO      3   // 信息级别
#define MY_BL_LOG_LEVEL_DEBUG     4   // 调试级别

/* 当前日志级别 */
#define MY_BL_LOG_CURRENT_LEVEL   MY_BL_LOG_LEVEL_INFO

/*********************************************************************
 * OTA 拷贝配置
 *********************************************************************/
#define BL_OTA_CHUNK_SIZE           (4096U)

/*********************************************************************
 * 系统时钟
 *********************************************************************/
#define BL_SYS_CLOCK_HZ             (120000000U)

/*********************************************************************
 * 错误码定义
 *********************************************************************/
#define BL_OK                       (0)
#define BL_ERR_FLASH                (-1)
#define BL_ERR_CRC                  (-2)    /* checksum 校验失败 */
#define BL_ERR_VERIFY               (-3)
#define BL_ERR_INVALID              (-4)
#define BL_ERR_TIMEOUT              (-5)
#define BL_ERR_MAGIC                (-6)    /* magic 校验失败 */

#endif /* __MY_BL_CONFIG_H__ */
