/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       my_tq.c
**文件描述：       异步发送队列模块实现
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.06.11
*********************************************************************
** 功能描述：       1. 提供基于动态内存的异步发送队列
**                 2. 支持数据缓存、流控保护、自动发送
**                 3. 适用于 UART/SPI/I2C 等异步发送场景
**                 4. 队列深度限制、统计信息
*********************************************************************
** 修改记录：       V1.0 2026.06.11 初始版本（含队列深度限制、统计信息、
**                                 deinit函数、flush安全修复、
**                                 参数类型优化、is_empty接口）
*********************************************************************/

#include "my_tq.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

/*********************************************************************
 * 内部辅助函数声明
 *********************************************************************/
static my_tq_node_t *my_tq_pop(my_tq_ctrl_t *queue);
static void my_tq_free_node(my_tq_node_t *node);

/*********************************************************************
 * 公开API实现
 *********************************************************************/

/*********************************************************************
 * @brief   初始化发送队列
 * @param   queue      队列控制块指针
 * @param   max_count  最大队列深度（0表示无限制）
 * @return  0: 成功  -1: 失败
 *********************************************************************/
int my_tq_init(my_tq_ctrl_t *queue, uint16_t max_count)
{
    if (queue == NULL)
    {
        return -1;
    }

    queue->head = NULL;
    queue->tail = NULL;
    queue->current = NULL;
    queue->count = 0;
    queue->max_count = max_count;
    queue->is_tx_busy = false;

    /* 初始化统计信息 */
    queue->push_count = 0;
    queue->pop_count = 0;
    queue->alloc_fail = 0;
    queue->peak_count = 0;

    return 0;
}

/*********************************************************************
 * @brief   将数据推入发送队列
 * @param   queue  队列控制块指针
 * @param   data   数据指针（只读）
 * @param   len    数据长度
 * @return  0: 成功  -1: 失败
 * @note    动态分配内存拷贝数据。
 * @warning 此函数不是中断安全的，不应在中断回调中直接调用。
 * @note    推荐用法：中断回调仅发送消息，任务上下文中调用此函数。
 *********************************************************************/
int my_tq_push(my_tq_ctrl_t *queue, const uint8_t *data, uint16_t len)
{
    my_tq_node_t *node;

    if (queue == NULL || data == NULL || len == 0)
    {
        return -1;
    }

    /* 检查队列深度限制 */
    if (queue->max_count > 0 && queue->count >= queue->max_count)
    {
        queue->alloc_fail++;
        return -1;
    }

    /* 动态分配节点 */
    node = (my_tq_node_t *)pvPortMalloc(sizeof(my_tq_node_t));
    if (node == NULL)
    {
        queue->alloc_fail++;
        return -1;
    }

    /* 动态分配数据缓冲区 */
    node->data = (uint8_t *)pvPortMalloc(len);
    if (node->data == NULL)
    {
        vPortFree(node);
        queue->alloc_fail++;
        return -1;
    }

    /* 拷贝数据 */
    memcpy(node->data, data, len);
    node->len = len;
    node->next = NULL;

    /* 加入队列 */
    if (queue->tail == NULL)
    {
        queue->head = node;
        queue->tail = node;
    }
    else
    {
        queue->tail->next = node;
        queue->tail = node;
    }
    queue->count++;

    /* 更新统计信息 */
    queue->push_count++;
    if (queue->count > queue->peak_count)
    {
        queue->peak_count = queue->count;
    }

    return 0;
}

/*********************************************************************
 * @brief   从队列弹出节点
 * @param   queue  队列控制块指针
 * @return  节点指针，队列为空返回 NULL
 *********************************************************************/
static my_tq_node_t *my_tq_pop(my_tq_ctrl_t *queue)
{
    my_tq_node_t *node;

    if (queue == NULL || queue->head == NULL)
    {
        return NULL;
    }

    /* 取出头节点 */
    node = queue->head;
    queue->head = node->next;

    if (queue->head == NULL)
    {
        queue->tail = NULL;
    }

    queue->count--;
    node->next = NULL;

    /* 更新统计信息 */
    queue->pop_count++;

    return node;
}

/*********************************************************************
 * @brief   释放节点内存
 * @param   node  节点指针
 * @return  none
 *********************************************************************/
static void my_tq_free_node(my_tq_node_t *node)
{
    if (node == NULL)
    {
        return;
    }

    if (node->data != NULL)
    {
        vPortFree(node->data);
    }

    vPortFree(node);
}

/*********************************************************************
 * @brief   处理发送队列（触发发送）
 * @param   queue       队列控制块指针
 * @param   send_func   发送函数指针
 * @return  0: 成功（已触发发送或队列为空）  -1: 失败
 * @note    如果队列不为空且未在发送，则发送下一包
 *********************************************************************/
int my_tq_process(my_tq_ctrl_t *queue, my_tq_send_func_t send_func)
{
    my_tq_node_t *node;
    int ret;

    if (queue == NULL || send_func == NULL)
    {
        return -1;
    }

    /* 如果正在发送或队列为空，则返回 */
    if (queue->is_tx_busy || queue->head == NULL)
    {
        return 0;
    }

    /* 弹出下一包 */
    node = my_tq_pop(queue);
    if (node == NULL)
    {
        return 0;
    }

    /* 保存当前发送节点 */
    queue->current = node;

    /* 异步发送 */
    queue->is_tx_busy = true;

    ret = send_func(node->data, node->len);
    if (ret < 0 || ret != node->len)
    {
        /* 发送失败：错误码或部分发送，丢弃该数据包 */
        queue->is_tx_busy = false;
        queue->current = NULL;
        my_tq_free_node(node);
        return -1;
    }

    return 0;
}

/*********************************************************************
 * @brief   发送完成回调处理
 * @param   queue  队列控制块指针
 * @return  none
 * @note    在发送完成中断或消息处理中调用，释放内存并触发下一包
 *********************************************************************/
void my_tq_tx_done(my_tq_ctrl_t *queue)
{
    if (queue == NULL)
    {
        return;
    }

    configASSERT(queue->is_tx_busy == true);
    configASSERT(queue->current != NULL);

    /* 释放已发送节点的内存 */
    if (queue->current != NULL)
    {
        my_tq_free_node(queue->current);
        queue->current = NULL;
    }

    /* 标记发送空闲 */
    queue->is_tx_busy = false;
}

/*********************************************************************
 * @brief   反初始化发送队列
 * @param   queue  队列控制块指针
 * @return  none
 * @note    释放所有队列节点内存，重置控制块
 *********************************************************************/
void my_tq_deinit(my_tq_ctrl_t *queue)
{
    my_tq_node_t *node;

    if (queue == NULL)
    {
        return;
    }

    /* 清空等待队列中的所有节点 */
    while ((node = my_tq_pop(queue)) != NULL)
    {
        my_tq_free_node(node);
    }

    /* 强制释放 current 节点（即使正在发送） */
    /* 注意：如果硬件仍在发送，可能导致访问非法内存，
     * 建议在调用 deinit 前先等待 TX_DONE 或确保硬件已停止 */
    if (queue->current != NULL)
    {
        my_tq_free_node(queue->current);
        queue->current = NULL;
    }

    /* 重置所有字段 */
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
    queue->max_count = 0;
    queue->is_tx_busy = false;
    queue->push_count = 0;
    queue->pop_count = 0;
    queue->alloc_fail = 0;
    queue->peak_count = 0;
}

/*********************************************************************
 * @brief   检查队列是否为空
 * @param   queue  队列控制块指针
 * @return  true: 空  false: 非空
 *********************************************************************/
bool my_tq_is_empty(my_tq_ctrl_t *queue)
{
    if (queue == NULL)
    {
        return true;
    }

    return (queue->count == 0);
}

/*********************************************************************
 * @brief   获取队列中待发送的数据包数量
 * @param   queue  队列控制块指针
 * @return  数据包数量
 *********************************************************************/
uint16_t my_tq_get_count(my_tq_ctrl_t *queue)
{
    if (queue == NULL)
    {
        return 0;
    }

    return queue->count;
}

/*********************************************************************
 * @brief   检查队列是否正在发送
 * @param   queue  队列控制块指针
 * @return  true: 正在发送  false: 空闲
 *********************************************************************/
bool my_tq_is_busy(my_tq_ctrl_t *queue)
{
    if (queue == NULL)
    {
        return false;
    }

    return queue->is_tx_busy;
}

/*********************************************************************
 * @brief   清空发送队列
 * @param   queue  队列控制块指针
 * @return  none
 * @note    如果正在发送（is_tx_busy=true），仅清空等待队列（保留 current）；
 *          如果未在发送，清空所有节点（包括 current）。
 *          正在发送时不会修改 is_tx_busy 状态，避免硬件访问异常。
 *********************************************************************/
void my_tq_flush(my_tq_ctrl_t *queue)
{
    my_tq_node_t *node;

    if (queue == NULL)
    {
        return;
    }

    /* 如果正在发送，不能清空 current 节点（硬件可能正在使用） */
    if (queue->is_tx_busy)
    {
        /* 仅清空等待队列 */
        while ((node = my_tq_pop(queue)) != NULL)
        {
            my_tq_free_node(node);
        }
        return;
    }

    /* 释放所有待发送节点 */
    while ((node = my_tq_pop(queue)) != NULL)
    {
        my_tq_free_node(node);
    }

    /* 释放 current 节点（仅在非发送状态） */
    if (queue->current != NULL)
    {
        my_tq_free_node(queue->current);
        queue->current = NULL;
    }

    queue->is_tx_busy = false;
}

/*********************************************************************
 * @brief   获取队列统计信息
 * @param   queue        队列控制块指针
 * @param   push_count   输出：累计入队次数（可为NULL）
 * @param   pop_count    输出：累计出队次数（可为NULL）
 * @param   alloc_fail   输出：内存分配失败次数（可为NULL）
 * @param   peak_count   输出：队列峰值（可为NULL）
 * @return  none
 *********************************************************************/
void my_tq_get_stats(my_tq_ctrl_t *queue, uint32_t *push_count, uint32_t *pop_count,
                     uint32_t *alloc_fail, uint16_t *peak_count)
{
    if (queue == NULL)
    {
        return;
    }

    if (push_count != NULL)
    {
        *push_count = queue->push_count;
    }
    if (pop_count != NULL)
    {
        *pop_count = queue->pop_count;
    }
    if (alloc_fail != NULL)
    {
        *alloc_fail = queue->alloc_fail;
    }
    if (peak_count != NULL)
    {
        *peak_count = queue->peak_count;
    }
}

/*********************************************************************
 * @brief   重置队列统计信息
 * @param   queue  队列控制块指针
 * @return  none
 *********************************************************************/
void my_tq_reset_stats(my_tq_ctrl_t *queue)
{
    if (queue == NULL)
    {
        return;
    }

    queue->push_count = 0;
    queue->pop_count = 0;
    queue->alloc_fail = 0;
    queue->peak_count = 0;
}
