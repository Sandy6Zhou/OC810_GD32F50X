/*!
    \file    gd32f50x_it.c
    \brief   interrupt service routines

    \version 2026-04-16, V1.0.0, demo for GD32F50x with FreeRTOS
*/

/*
    Copyright (c) 2026, GigaDevice Semiconductor Inc.

    Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors
       may be used to endorse or promote products derived from this software without
       specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
OF SUCH DAMAGE.
*/

#include "gd32f50x_it.h"
#include "FreeRTOS.h"
#include "task.h"
#include "my_log.h"
#include "timer_driver.h"

#define SRAM_ECC_ERROR_HANDLE(s)    do{}while(1)

/*!
    \brief      this function handles NMI exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
void NMI_Handler(void)
{
    if(SET == syscfg_sram_ecc_flag_get(SYSCFG_SRAMECCSTAT_SRAMECCMEIF)) {
        SRAM_ECC_ERROR_HANDLE("SRAM non-correction event detected\r\n");
    } else if(SET == syscfg_sram_ecc_flag_get(SYSCFG_SRAMECCSTAT_SRAMECCSEIF)) {
        SRAM_ECC_ERROR_HANDLE("SRAM single bit correction event detected\r\n");
    } else {
        /* if NMI exception occurs, go to infinite loop */
        /* HXTAL clock monitor NMI error or NMI pin error */
        while(1) {
        }
    }
}

/*!
    \brief      this function handles HardFault exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
void HardFault_Handler(void)
{
    /* 输出 HardFault 信息 */
    my_log_init();
    MY_LOG_E("[HARDFAULT] HardFault exception occurred!");
    MY_LOG_E("[HARDFAULT] SCB->CFSR: 0x%08X", *(volatile uint32_t *)0xE000ED28);

    /* if Hard Fault exception occurs, go to infinite loop */
    while (1){
    }
}

/*!
    \brief      this function handles MemManage exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
void MemManage_Handler(void)
{
    /* if Memory Manage exception occurs, go to infinite loop */
    while (1){
    }
}

/*!
    \brief      this function handles BusFault exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
void BusFault_Handler(void)
{
    /* if Bus Fault exception occurs, go to infinite loop */
    while (1){
    }
}

/*!
    \brief      this function handles UsageFault exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
void UsageFault_Handler(void)
{
    /* if Usage Fault exception occurs, go to infinite loop */
    while (1){
    }
}

/*!
    \brief      this function handles SVC exception
    \param[in]  none
    \param[out] none
    \retval     none
    \note       This handler is implemented by FreeRTOS port layer
*/
// SVC_Handler is handled by FreeRTOS port

/*!
    \brief      this function handles DebugMon exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
void DebugMon_Handler(void)
{
    /* if DebugMon exception occurs, go to infinite loop */
    while(1) {
    }
}

/*!
    \brief      this function handles PendSV exception
    \param[in]  none
    \param[out] none
    \retval     none
    \note       This handler is implemented by FreeRTOS port layer
*/
// PendSV_Handler is handled by FreeRTOS port

/*!
    \brief      this function handles SysTick exception
    \param[in]  none
    \param[out] none
    \retval     none
    \note       This handler is implemented by FreeRTOS port layer
*/
// SysTick_Handler is handled by FreeRTOS port

/*********************************************************************
 * Timer 中断服务函数
 *
 * 设计说明：
 *   1. 每个 TIMER 独立实现 ISR，检查 UPDATE 中断标志
 *   2. 清除中断标志后，调用 DRV_TIMER_ISR_CALLBACK 宏执行回调
 *   3. 保持标准项目架构（ISR 在 it.c 中）
 *********************************************************************/

/*********************************************************************
 * @brief   TIMER0 UPDATE中断服务函数
 * @note    检查并清除UPDATE中断标志，执行UPDATE回调函数
 *********************************************************************/
void TIMER0_UP_IRQHandler(void)
{
    if(SET == timer_interrupt_flag_get(TIMER0, TIMER_INT_FLAG_UP))
    {
        timer_interrupt_flag_clear(TIMER0, TIMER_INT_FLAG_UP);
        drv_timer_run_update_callback(DRV_TIMER_0);
    }
}

/*********************************************************************
 * @brief   TIMER1 UPDATE中断服务函数
 * @note    检查并清除UPDATE中断标志，执行UPDATE回调函数
 *********************************************************************/
void TIMER1_IRQHandler(void)
{
    if(SET == timer_interrupt_flag_get(TIMER1, TIMER_INT_FLAG_UP))
    {
        timer_interrupt_flag_clear(TIMER1, TIMER_INT_FLAG_UP);
        drv_timer_run_update_callback(DRV_TIMER_1);
    }
}

/*********************************************************************
 * @brief   TIMER2 UPDATE中断服务函数
 * @note    检查并清除UPDATE中断标志，执行UPDATE回调函数
 *********************************************************************/
void TIMER2_IRQHandler(void)
{
    if(SET == timer_interrupt_flag_get(TIMER2, TIMER_INT_FLAG_UP))
    {
        timer_interrupt_flag_clear(TIMER2, TIMER_INT_FLAG_UP);
        drv_timer_run_update_callback(DRV_TIMER_2);
    }
}

/*********************************************************************
 * @brief   TIMER3 UPDATE中断服务函数
 * @note    检查并清除UPDATE中断标志，执行UPDATE回调函数
 *********************************************************************/
void TIMER3_IRQHandler(void)
{
    if(SET == timer_interrupt_flag_get(TIMER3, TIMER_INT_FLAG_UP))
    {
        timer_interrupt_flag_clear(TIMER3, TIMER_INT_FLAG_UP);
        drv_timer_run_update_callback(DRV_TIMER_3);
    }
}

/*********************************************************************
 * @brief   TIMER4 UPDATE中断服务函数
 * @note    检查并清除UPDATE中断标志，执行UPDATE回调函数
 *********************************************************************/
void TIMER4_IRQHandler(void)
{
    if(SET == timer_interrupt_flag_get(TIMER4, TIMER_INT_FLAG_UP))
    {
        timer_interrupt_flag_clear(TIMER4, TIMER_INT_FLAG_UP);
        drv_timer_run_update_callback(DRV_TIMER_4);
    }
}

/*********************************************************************
 * @brief   TIMER5 UPDATE中断服务函数
 * @note    检查并清除UPDATE中断标志，执行UPDATE回调函数
 *********************************************************************/
void TIMER5_IRQHandler(void)
{
    if(SET == timer_interrupt_flag_get(TIMER5, TIMER_INT_FLAG_UP))
    {
        timer_interrupt_flag_clear(TIMER5, TIMER_INT_FLAG_UP);
        drv_timer_run_update_callback(DRV_TIMER_5);
    }
}

/*********************************************************************
 * @brief   TIMER6 UPDATE中断服务函数
 * @note    检查并清除UPDATE中断标志，执行UPDATE回调函数
 *********************************************************************/
void TIMER6_IRQHandler(void)
{
    if(SET == timer_interrupt_flag_get(TIMER6, TIMER_INT_FLAG_UP))
    {
        timer_interrupt_flag_clear(TIMER6, TIMER_INT_FLAG_UP);
        drv_timer_run_update_callback(DRV_TIMER_6);
    }
}

/*********************************************************************
 * @brief   TIMER7 UPDATE中断服务函数
 * @note    检查并清除UPDATE中断标志，执行UPDATE回调函数
 *********************************************************************/
void TIMER7_UP_IRQHandler(void)
{
    if(SET == timer_interrupt_flag_get(TIMER7, TIMER_INT_FLAG_UP))
    {
        timer_interrupt_flag_clear(TIMER7, TIMER_INT_FLAG_UP);
        drv_timer_run_update_callback(DRV_TIMER_7);
    }
}

/*********************************************************************
 * @brief   TIMER15 UPDATE中断服务函数
 * @note    检查并清除UPDATE中断标志，执行UPDATE回调函数
 *********************************************************************/
void TIMER15_IRQHandler(void)
{
    if(SET == timer_interrupt_flag_get(TIMER15, TIMER_INT_FLAG_UP))
    {
        timer_interrupt_flag_clear(TIMER15, TIMER_INT_FLAG_UP);
        drv_timer_run_update_callback(DRV_TIMER_15);
    }
}

/*********************************************************************
 * @brief   TIMER16 UPDATE中断服务函数
 * @note    检查并清除UPDATE中断标志，执行UPDATE回调函数
 *********************************************************************/
void TIMER16_IRQHandler(void)
{
    if(SET == timer_interrupt_flag_get(TIMER16, TIMER_INT_FLAG_UP))
    {
        timer_interrupt_flag_clear(TIMER16, TIMER_INT_FLAG_UP);
        drv_timer_run_update_callback(DRV_TIMER_16);
    }
}
