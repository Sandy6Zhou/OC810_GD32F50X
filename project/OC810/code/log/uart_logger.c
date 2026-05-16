/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       uart_logger.c
**文件描述：       串口日志底层实现 (USART0, 115200波特率)
**当前版本：       V1.0
**作    者：       伍玉蛟 (wuyujiao@jimiiot.com)
**完成日期：       2026.04.17
*********************************************************************
** 功能描述：       1. 实现 USART0 初始化（PA9引脚，115200波特率）
**                 2. 实现串口数据轮询发送功能
*********************************************************************/

#include "uart_logger.h"
#include "gd32f50x.h"

/*********************************************************************
 * @brief   初始化串口日志
 * @param   none
 * @return  none
 * @note    使用USART0，PA9引脚，波特率115200
 *********************************************************************/
void uart_logger_init(void)
{
    /* 使能时钟 */
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_USART0);

    /* 配置PA9为USART0_TX */
    gpio_af_set(GPIOA, GPIO_AF_7, GPIO_PIN_9);
    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_9);
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_LEVEL3, GPIO_PIN_9);

    /* 配置USART0参数 */
    usart_deinit(USART0);
    usart_baudrate_set(USART0, 115200U);
    usart_word_length_set(USART0, USART_WL_8BIT);
    usart_stop_bit_set(USART0, USART_STB_1BIT);
    usart_parity_config(USART0, USART_PM_NONE);
    usart_hardware_flow_rts_config(USART0, USART_RTS_DISABLE);
    usart_hardware_flow_cts_config(USART0, USART_CTS_DISABLE);
    usart_receive_config(USART0, USART_RECEIVE_DISABLE);
    usart_transmit_config(USART0, USART_TRANSMIT_ENABLE);
    usart_enable(USART0);
}

/*********************************************************************
 * @brief   通过串口写入数据
 * @param   data 数据指针
 * @param   len 数据长度
 * @return  实际写入的字节数
 * @note    轮询方式发送，阻塞等待发送完成
 *********************************************************************/
int uart_logger_write(const char *data, int len)
{
    int i;

    for (i = 0; i < len; i++)
    {
        /* 等待发送缓冲区为空 */
        while (usart_flag_get(USART0, USART_FLAG_TBE) == RESET)
        {
        }

        /* 发送数据 */
        usart_data_transmit(USART0, (uint8_t)data[i]);
    }

    return len;
}
