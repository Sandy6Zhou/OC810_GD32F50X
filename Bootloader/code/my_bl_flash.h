/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       my_bl_flash.h
**文件描述：       Bootloader FLASH操作接口头文件
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.06.22
*********************************************************************
** 功能描述：       FLASH擦除、编程、读取接口声明
*********************************************************************/

#ifndef __MY_BL_FLASH_H__
#define __MY_BL_FLASH_H__

#include <stdint.h>

/*********************************************************************
 * @brief   擦除指定地址范围的FLASH页
 * @param   addr 起始地址（需页对齐）
 * @param   size 擦除大小（字节）
 * @return  BL_OK成功，BL_ERR_FLASH失败
 * @note    addr必须为BL_FLASH_PAGE_SIZE整数倍
 *********************************************************************/
int my_bl_flash_erase(uint32_t addr, uint32_t size);

/*********************************************************************
 * @brief   编程FLASH（按字写入）
 * @param   addr 写入起始地址
 * @param   data 数据指针
 * @param   size 数据大小（字节，需4字节对齐）
 * @return  BL_OK成功，BL_ERR_FLASH失败，BL_ERR_VERIFY校验失败
 * @note    写入后逐字验证
 *********************************************************************/
int my_bl_flash_program(uint32_t addr, const uint8_t *data, uint32_t size);

/*********************************************************************
 * @brief   读取FLASH数据
 * @param   addr 读取起始地址
 * @param   buf  输出缓冲区
 * @param   size 读取大小（字节）
 * @return  BL_OK成功，BL_ERR_INVALID参数无效
 *********************************************************************/
int my_bl_flash_read(uint32_t addr, uint8_t *buf, uint32_t size);

#endif /* __MY_BL_FLASH_H__ */
