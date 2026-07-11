/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       my_rb.c
**文件描述：       环形缓冲区模块实现文件
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.04.21
*********************************************************************
** 功能描述：       1. 实现环形缓冲区的初始化、读写操作
**                 2. 提供缓冲区状态查询和清空功能
**                 3. 实现满、空、溢出保护机制
*********************************************************************/

#include "my_rb.h"
#include <string.h>

/*********************************************************************
 * 内部辅助宏定义
 *********************************************************************/

/**
 * @brief 获取下一个位置（循环）
 */
#define MY_RB_NEXT_POS(rb, pos) (((pos) + 1) % (rb)->size)

/**
 * @brief 计算当前数据量（中断安全：通过 head/tail 计算）
 * @note  head 和 tail 都是 volatile，读取时保证原子性（32位对齐）
 */
#define MY_RB_GET_COUNT(rb) (((rb)->head >= (rb)->tail) ? \
                             ((rb)->head - (rb)->tail) : \
                             ((rb)->size - (rb)->tail + (rb)->head))

/*********************************************************************
 * 函数实现
 *********************************************************************/

/*********************************************************************
 * @brief   初始化环形缓冲区
 * @param   rb      环形缓冲区控制结构体指针
 * @param   buf     缓冲区数据指针（由应用层分配）
 * @param   size    缓冲区大小（字节）
 * @return  0表示成功，-1表示失败（参数错误）
 * @note    应用层负责分配buf内存，驱动层仅使用
 *********************************************************************/
int my_rb_init(my_rb_t *rb, uint8_t *buf, uint32_t size)
{
    if (rb == NULL || buf == NULL || size == 0)
    {
        return -1;
    }

    rb->buf = buf;
    rb->size = size;
    rb->head = 0;
    rb->tail = 0;

    return 0;
}

/*********************************************************************
 * @brief   向环形缓冲区写入数据
 * @param   rb      环形缓冲区控制结构体指针
 * @param   data    待写入数据指针
 * @param   len     待写入数据长度（字节）
 * @return  实际写入的字节数，-1表示失败
 * @note    如果缓冲区空间不足，仅写入可容纳的部分
 *********************************************************************/
int my_rb_write(my_rb_t *rb, const uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint32_t write_len;
    uint32_t count;

    if (rb == NULL || data == NULL || len == 0)
    {
        return -1;
    }

    /* 计算实际可写入的长度 */
    count = MY_RB_GET_COUNT(rb);
    write_len = (len < (rb->size - count)) ? len : (rb->size - count);

    if (write_len == 0)
    {
        return 0; /* 缓冲区已满 */
    }

    for (i = 0; i < write_len; i++)
    {
        rb->buf[rb->head] = data[i];
        rb->head = MY_RB_NEXT_POS(rb, rb->head);
    }

    return (int)write_len;
}

/*********************************************************************
 * @brief   从环形缓冲区读取数据
 * @param   rb      环形缓冲区控制结构体指针
 * @param   data    读取数据存放指针
 * @param   len     期望读取的数据长度（字节）
 * @return  实际读取的字节数，-1表示失败
 * @note    如果缓冲区数据不足，仅读取现有数据
 *********************************************************************/
int my_rb_read(my_rb_t *rb, uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint32_t read_len;
    uint32_t count;

    if (rb == NULL || data == NULL || len == 0)
    {
        return -1;
    }

    /* 计算实际可读取的长度 */
    count = MY_RB_GET_COUNT(rb);
    read_len = (len < count) ? len : count;

    if (read_len == 0)
    {
        return 0; /* 缓冲区为空 */
    }

    for (i = 0; i < read_len; i++)
    {
        data[i] = rb->buf[rb->tail];
        rb->tail = MY_RB_NEXT_POS(rb, rb->tail);
    }

    return (int)read_len;
}

/*********************************************************************
 * @brief   清空环形缓冲区
 * @param   rb      环形缓冲区控制结构体指针
 * @return  0表示成功，-1表示失败
 * @note    无
 *********************************************************************/
int my_rb_clear(my_rb_t *rb)
{
    if (rb == NULL)
    {
        return -1;
    }

    rb->head = 0;
    rb->tail = 0;

    return 0;
}

/*********************************************************************
 * @brief   获取环形缓冲区中剩余可写空间
 * @param   rb      环形缓冲区控制结构体指针
 * @return  可写字节数，-1表示失败
 * @note    无
 *********************************************************************/
int my_rb_get_free_size(const my_rb_t *rb)
{
    if (rb == NULL)
    {
        return -1;
    }

    return (int)(rb->size - MY_RB_GET_COUNT(rb));
}

/*********************************************************************
 * @brief   获取环形缓冲区中已存数据量
 * @param   rb      环形缓冲区控制结构体指针
 * @return  已存字节数，-1表示失败
 * @note    无
 *********************************************************************/
int my_rb_get_data_size(const my_rb_t *rb)
{
    if (rb == NULL)
    {
        return -1;
    }

    return (int)MY_RB_GET_COUNT(rb);
}

/*********************************************************************
 * @brief   判断环形缓冲区是否为空
 * @param   rb      环形缓冲区控制结构体指针
 * @return  true表示空，false表示非空
 * @note    无
 *********************************************************************/
bool my_rb_is_empty(const my_rb_t *rb)
{
    if (rb == NULL)
    {
        return true;
    }

    return (MY_RB_GET_COUNT(rb) == 0);
}

/*********************************************************************
 * @brief   判断环形缓冲区是否已满
 * @param   rb      环形缓冲区控制结构体指针
 * @return  true表示满，false表示未满
 * @note    无
 *********************************************************************/
bool my_rb_is_full(const my_rb_t *rb)
{
    if (rb == NULL)
    {
        return true;
    }

    return (MY_RB_GET_COUNT(rb) == rb->size);
}
