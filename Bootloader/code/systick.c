/*!
    \file    systick.c
    \brief   the systick configuration file

    \version 2026-02-25, V1.0.4, firmware for GD32F50x
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

#include "gd32f50x.h"
#include "systick.h"

volatile static uint32_t delay;

/* 系统运行时间（毫秒），由SysTick中断递增 */
static volatile uint32_t sTickMs = 0U;

/* 延时递减计数器 */
static volatile uint32_t sDelayMs = 0U;

/*!
    \brief      configure systick
    \param[in]  none
    \param[out] none
    \retval     none
*/
void systick_config(void)
{
    /* setup systick timer for 1000Hz interrupts */
    if (SysTick_Config(SystemCoreClock / 1000U)){
        /* capture error */
        while (1){
        }
    }
    /* configure the systick handler priority */
    NVIC_SetPriority(SysTick_IRQn, 0x00U);
}

/*!
    \brief      delay a time in milliseconds
    \param[in]  count: count in milliseconds
    \param[out] none
    \retval     none
*/
void delay_1ms(uint32_t count)
{
    delay = count;

    while(0U != delay){
    }
}

/*!
    \brief      delay decrement
    \param[in]  none
    \param[out] none
    \retval     none
*/
void delay_decrement(void)
{
    if (0U != delay){
        delay--;
    }
}

/*********************************************************************
 * 用户函数实现
 *********************************************************************/

 /*********************************************************************
 * @brief   获取系统运行时间（毫秒）
 * @param   none
 * @return  system run time in milliseconds
 *********************************************************************/
uint32_t my_bl_systick_get_ms(void)
{
    return sTickMs;
}

/*********************************************************************
 * @brief   延时ms
 * @param   ms: delay in milliseconds
 * @return  none
 *********************************************************************/
void my_bl_delay_ms(uint32_t ms)
{
    sDelayMs = ms;

    /* 等待中断递减至0 */
    while (sDelayMs > 0U)
    {
        /* 裸机环境无WFI，避免进入低功耗 */
    }
}

/*********************************************************************
 * @brief   SysTick中断处理函数
 * @param   none
 * @return  none
 *********************************************************************/
void my_bl_systick_handler(void)
{
    /* 递增系统tick计数器 */
    sTickMs++;

    /* 递减延时计数器 */
    if (sDelayMs > 0U)
    {
        sDelayMs--;
    }
}
