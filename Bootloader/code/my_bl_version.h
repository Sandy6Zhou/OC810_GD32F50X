/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       my_bl_version.h
**文件描述：       Bootloader版本信息定义文件
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.06.22
*********************************************************************
** 功能描述：       定义Bootloader版本号字符串与数值
*********************************************************************/

#ifndef __MY_BL_VERSION_H__
#define __MY_BL_VERSION_H__

/* 版本号数值 */
#define BL_VERSION_MAJOR        (1U)
#define BL_VERSION_MINOR        (0U)
#define BL_VERSION_PATCH        (0U)

/* 版本号字符串 */
#define BL_VERSION_STRING       "1.0.0"

/* 构建标识 */
#define BL_BUILD_DATE           __DATE__
#define BL_BUILD_TIME           __TIME__

#endif /* __MY_BL_VERSION_H__ */
