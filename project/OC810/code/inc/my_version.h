/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       my_version.h
**文件描述：       系统版本信息管理
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.06.05
*********************************************************************
** 功能描述：       统一管理固件版本号
** 版本格式：       JM-OC810MCU-STD-V1.0.260605.01
*********************************************************************/

#ifndef __MY_VERSION_H__
#define __MY_VERSION_H__

/*********************************************************************
 * 硬件版本定义
 *********************************************************************/

/** 硬件主版本号 */
#define MY_HW_VERSION_MAJOR   (1U)

/** 硬件次版本号 */
#define MY_HW_VERSION_MINOR   (0U)

/** 硬件版本字符串 */
#define MY_HW_VERSION_STRING  "HW:OC810-M-V1.0"

/*********************************************************************
 * 软件版本定义
 * 格式：JM-OC810MCU-STD-V1.0.260605.01
 *   JM       - 公司名（几米物联）
 *   OC810    - 产品型号
 *   MCU      - 平台类型（MCU=单片机，DVR=视频模块等）
 *   STD      - 版本类型（STD=标准版，CUS=客户定制版）
 *   V1.0     - 当前版本（主版本.次版本）
 *   260605   - 构建日期（YYMMDD）
 *   01       - 当天构建序号（01~99）
 *********************************************************************/

/** 公司标识 */
#define MY_SW_VERSION_COMPANY   "JM"

/** 产品型号 */
#define MY_SW_VERSION_PRODUCT   "OC810"

/** 平台类型（MCU=单片机，LNX=Linux） */
#define MY_SW_VERSION_PLATFORM  "MCU"

/** 版本类型（STD=标准版，CUS=客户定制版） */
#define MY_SW_VERSION_TYPE      "STD"

/** 主版本号（重大功能变更时递增） */
#define MY_SW_VERSION_MAJOR     (1U)

/** 次版本号（新功能添加时递增） */
#define MY_SW_VERSION_MINOR     (0U)

/** 构建日期（YYMMDD格式） */
#define MY_SW_VERSION_DATE      "260605"

/** 当天构建序号（01~99） */
#define MY_SW_VERSION_BUILD     "01"

/** 软件版本全称（自动拼接） */
#define MY_SW_VERSION_STRING    MY_SW_VERSION_COMPANY "-" \
                                 MY_SW_VERSION_PRODUCT \
                                 MY_SW_VERSION_PLATFORM "-" \
                                 MY_SW_VERSION_TYPE "-V" \
                                 #MY_SW_VERSION_MAJOR "." #MY_SW_VERSION_MINOR "." \
                                 MY_SW_VERSION_DATE "." \
                                 MY_SW_VERSION_BUILD

/** 软件版本信息（带构建时间，用于启动打印） */
#define MY_SW_VERSION_INFO      MY_SW_VERSION_STRING " (" __TIME__ ")"

#endif /* __MY_VERSION_H__ */
