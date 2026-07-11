/*******************************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       param_config.h
**文件描述：       参数管理系统配置文件
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.06.02
*******************************************************************************
** 功能描述：       1. 定义参数ID分配策略
**                 2. 定义参数存储格式
**                 3. 定义系统配置参数
*******************************************************************************/

#ifndef __PARAM_CONFIG_H__
#define __PARAM_CONFIG_H__

#include <stdint.h>

/*******************************************************************************
 * 参数ID分配策略（16位ID空间：0x0000 ~ 0xFFFF）
 ******************************************************************************/

/* 系统保留 */
#define PARAM_ID_RESERVED       0x0000

/* 系统配置（0x0001 ~ 0x0FFF，4096个） */
#define PARAM_ID_SYS_BASE       0x0001
#define PARAM_ID_SYS_MAX        0x0FFF

/* 网络配置（0x1000 ~ 0x1FFF，4096个） */
#define PARAM_ID_NET_BASE       0x1000
#define PARAM_ID_NET_MAX        0x1FFF

/* 设备参数（0x2000 ~ 0x2FFF，4096个） */
#define PARAM_ID_DEV_BASE       0x2000
#define PARAM_ID_DEV_MAX        0x2FFF

/* 用户设置（0x3000 ~ 0x3FFF，4096个） */
#define PARAM_ID_USR_BASE       0x3000
#define PARAM_ID_USR_MAX        0x3FFF

/* 校准数据（0x4000 ~ 0x4FFF，4096个） */
#define PARAM_ID_CAL_BASE       0x4000
#define PARAM_ID_CAL_MAX        0x4FFF

/* 预留（0x5000 ~ 0xEFFF，40960个） */
#define PARAM_ID_RSV_BASE       0x5000
#define PARAM_ID_RSV_MAX        0xEFFF

/* 系统保留（0xF000 ~ 0xFFFF） */
#define PARAM_ID_SYS_RSV_BASE   0xF000
#define PARAM_ID_SYS_RSV_MAX    0xFFFF

/*******************************************************************************
 * 参数存储格式
 ******************************************************************************/

/*******************************************************************************
 * @brief   参数文件头部结构
 *******************************************************************************/
typedef struct
{
    uint16_t    magic;          /**< 魔数: 0x504D ("PM") */
    uint16_t    version;        /**< 版本号: 1 */
    uint16_t    param_id;       /**< 参数ID */
    uint16_t    data_len;       /**< 数据长度 */
    uint32_t    crc32;          /**< CRC32校验 */
    uint8_t     data[];         /**< 可变长度数据 */
} param_header_t;

/* 参数魔数 */
#define PARAM_MAGIC             0x504D          /**< "PM" */
#define PARAM_VERSION           1               /**< 版本号 */

/*******************************************************************************
 * 系统配置
 ******************************************************************************/

/* 参数存储路径 */
#define PARAM_DIR_PATH          "/params"              /**< 参数目录 */
#define PARAM_FILE_FMT          "/params/%s_%04x.dat"  /**< 文件名格式 */

/* 参数分类前缀 */
#define PARAM_CAT_SYS           "sys"           /**< 系统配置 */
#define PARAM_CAT_NET           "net"           /**< 网络配置 */
#define PARAM_CAT_DEV           "dev"           /**< 设备参数 */
#define PARAM_CAT_USR           "usr"           /**< 用户设置 */
#define PARAM_CAT_CAL           "cal"           /**< 校准数据 */
#define PARAM_CAT_RSV           "res"           /**< 预留 */

/* 写缓存配置（可选） */
#define PARAM_CACHE_ENABLE      0               /**< 0: 禁用，1: 启用 */
#define PARAM_CACHE_FLUSH_TICK  (5000)          /**< 缓存刷新周期（5秒） */
#define PARAM_CACHE_MAX_ENTRIES (16)            /**< 最大缓存条目数 */

#endif /* __PARAM_CONFIG_H__ */
