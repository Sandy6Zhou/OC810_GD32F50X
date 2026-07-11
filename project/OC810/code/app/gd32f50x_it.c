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
#include "dma_driver.h"
#include "uart_driver.h"
#include "adc_driver.h"
#include "gpio_driver.h"
#include "can_driver.h"
#include "rtc_driver.h"

#define SRAM_ECC_ERROR_HANDLE(s)    do{}while(1)

/*!
    \brief      this function handles NMI exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
void NMI_Handler(void)
{
    if (SET == syscfg_sram_ecc_flag_get(SYSCFG_SRAMECCSTAT_SRAMECCMEIF))
    {
        SRAM_ECC_ERROR_HANDLE("SRAM non-correction event detected\r\n");
    }
    else if (SET == syscfg_sram_ecc_flag_get(SYSCFG_SRAMECCSTAT_SRAMECCSEIF))
    {
        SRAM_ECC_ERROR_HANDLE("SRAM single bit correction event detected\r\n");
    }
    else
    {
        /* if NMI exception occurs, go to infinite loop */
        /* HXTAL clock monitor NMI error or NMI pin error */
        while(1) { }
    }
}

/* HardFault 诊断处理函数（由裸函数调用，传入正确的栈帧指针） */
void hardfault_dump(uint32_t *sp_frame);

/*!
    \brief      this function handles HardFault exception (naked wrapper)
    \param[in]  none
    \param[out] none
    \retval     none
    \note        使用 naked 属性避免编译器压栈破坏 MSP，确保 sp_frame 指向真正的异常栈帧
*/
__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "TST LR, #4          \n"  /* 检查 LR.bit2 判断使用 MSP 还是 PSP */
        "ITE EQ              \n"
        "MRSEQ R0, MSP       \n"  /* R0 = MSP (异常栈帧起始) */
        "MRSNE R0, PSP       \n"  /* R0 = PSP */
        "B hardfault_dump    \n"  /* 跳转到 C 处理函数，R0 作为第一个参数 */
    );
}

/*!
    \brief      HardFault 诊断处理函数
    \param[in]  sp_frame: 异常栈帧指针 (R0-R3, R12, LR, PC, xPSR)
    \note        此时 sp_frame 指向 CPU 自动压栈的真正栈帧
*/
void hardfault_dump(uint32_t *sp_frame)
{
    /* 使用 CMSIS 标准 SCB 结构体访问故障寄存器 */
    uint32_t cfsr = SCB->CFSR;
    uint32_t hfsr = SCB->HFSR;
    uint32_t mmfar = SCB->MMFAR;
    uint32_t bfar = SCB->BFAR;
    volatile uint32_t i;

    /* 无锁日志初始化（跳过互斥锁创建，仅初始化底层输出通道） */
    my_log_critical_init();

    my_log_critical_print("===== APP HARDFAULT OCCURRED =====");
    my_log_critical_print("CFSR: 0x%08X", cfsr);
    my_log_critical_print("HFSR: 0x%08X", hfsr);
    my_log_critical_print("MMFAR: 0x%08X", mmfar);
    my_log_critical_print("BFAR: 0x%08X", bfar);

    /* CFSR 详细解析 (MMFSR[7:0] + BFSR[15:8] + UFSR[31:16]) */
    /* MMFSR (MemManage Fault Status Register) */
    if (cfsr & SCB_CFSR_IACCVIOL_Msk)   my_log_critical_print("  -> IACCVIOL: Instruction access violation");
    if (cfsr & SCB_CFSR_DACCVIOL_Msk)   my_log_critical_print("  -> DACCVIOL: Data access violation");
    if (cfsr & SCB_CFSR_MUNSTKERR_Msk)  my_log_critical_print("  -> MUNSTKERR: MemManage fault on unstacking");
    if (cfsr & SCB_CFSR_MSTKERR_Msk)    my_log_critical_print("  -> MSTKERR: MemManage fault on stacking");
    if (cfsr & SCB_CFSR_MLSPERR_Msk)    my_log_critical_print("  -> MLSPERR: MemManage lazy save error");
    if (cfsr & SCB_CFSR_MMARVALID_Msk)  my_log_critical_print("  -> MMARVALID: MMFAR address valid");

    /* BFSR (BusFault Status Register) */
    if (cfsr & SCB_CFSR_IBUSERR_Msk)     my_log_critical_print("  -> IBUSERR: Instruction bus error");
    if (cfsr & SCB_CFSR_PRECISERR_Msk)   my_log_critical_print("  -> PRECISERR: Precise data bus error");
    if (cfsr & SCB_CFSR_IMPRECISERR_Msk) my_log_critical_print("  -> IMPRECISERR: Imprecise data bus error");
    if (cfsr & SCB_CFSR_UNSTKERR_Msk)    my_log_critical_print("  -> UNSTKERR: Bus fault on unstacking");
    if (cfsr & SCB_CFSR_STKERR_Msk)      my_log_critical_print("  -> STKERR: Bus fault on stacking");
    if (cfsr & SCB_CFSR_LSPERR_Msk)      my_log_critical_print("  -> LSPERR: Bus lazy save error");
    if (cfsr & SCB_CFSR_BFARVALID_Msk)   my_log_critical_print("  -> BFARVALID: BFAR address valid");

    /* UFSR (UsageFault Status Register) */
    if (cfsr & SCB_CFSR_UNDEFINSTR_Msk)  my_log_critical_print("  -> UNDEFINSTR: Undefined instruction");
    if (cfsr & SCB_CFSR_INVSTATE_Msk)    my_log_critical_print("  -> INVSTATE: Invalid state (Thumb/ARM)");
    if (cfsr & SCB_CFSR_INVPC_Msk)       my_log_critical_print("  -> INVPC: Invalid PC load (EXC_RETURN)");
    if (cfsr & SCB_CFSR_NOCP_Msk)        my_log_critical_print("  -> NOCP: No coprocessor");
    if (cfsr & SCB_CFSR_STKOF_Msk)       my_log_critical_print("  -> STKOF: Stack overflow");
    if (cfsr & SCB_CFSR_UNALIGNED_Msk)   my_log_critical_print("  -> UNALIGNED: Unaligned access");
    if (cfsr & SCB_CFSR_DIVBYZERO_Msk)   my_log_critical_print("  -> DIVBYZERO: Divide by zero");

    /* HFSR (HardFault Status Register) */
    if (hfsr & SCB_HFSR_VECTTBL_Msk)     my_log_critical_print("  -> VECTTBL: Vector table read error");
    if (hfsr & SCB_HFSR_FORCED_Msk)      my_log_critical_print("  -> FORCED: Forced hard fault (escalated)");
    if (hfsr & SCB_HFSR_DEBUGEVT_Msk)    my_log_critical_print("  -> DEBUGEVT: Debug event occurred");

    /* 栈帧寄存器快照（sp_frame 指向真正的异常栈帧） */
    my_log_critical_print("===== Stack Frame =====");
    my_log_critical_print("R0  = 0x%08X", sp_frame[0]);
    my_log_critical_print("R1  = 0x%08X", sp_frame[1]);
    my_log_critical_print("R2  = 0x%08X", sp_frame[2]);
    my_log_critical_print("R3  = 0x%08X", sp_frame[3]);
    my_log_critical_print("R12 = 0x%08X", sp_frame[4]);
    my_log_critical_print("LR  = 0x%08X", sp_frame[5]);
    my_log_critical_print("PC  = 0x%08X", sp_frame[6]);
    my_log_critical_print("xPSR= 0x%08X", sp_frame[7]);
    my_log_critical_print("MSP = 0x%08X", __get_MSP());
    my_log_critical_print("PSP = 0x%08X", __get_PSP());
    my_log_critical_print("===== END =====");

    /* 延时确保日志输出完成 */
    for (i = 0U; i < 5000000U; i++) {}

    /* 进入死循环，等待看门狗溢出复位 */
    while (1) { }
}

/*!
    \brief      this function handles MemManage exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
void MemManage_Handler(void)
{
    /* 使用 CMSIS 标准 SCB 结构体访问故障寄存器（提高可移植性） */
    uint32_t cfsr = SCB->CFSR;   /* Configurable Fault Status Register */
    uint32_t mmfar = SCB->MMFAR; /* MemManage Fault Address Register */

    /* 无锁日志初始化（跳过互斥锁创建，仅初始化底层输出通道） */
    my_log_critical_init();
    my_log_critical_print("===== MEMMANAGE FAULT OCCURRED =====");
    my_log_critical_print("CFSR: 0x%08X", cfsr);
    my_log_critical_print("MMFAR: 0x%08X", mmfar);

    /* 进入死循环，等待看门狗溢出复位 */
    while (1) { }
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
    while (1) { }
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
    while (1) { }
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
    while(1) { }
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

/*********************************************************************
 * DMA 中断服务函数
 *
 * 设计说明：
 *   1. 每个 DMA 通道独立实现 ISR，检查 FTF/HTF/ERR 中断标志
 *   2. 清除中断标志后，调用 drv_dma_run_callback 执行回调
 *   3. 遵循原厂示例规范：使用 DMA_INT_FLAG_GIF 清除标志
 *********************************************************************/

/*********************************************************************
 * @brief   DMA0 Channel 0 中断服务函数
 *********************************************************************/
void DMA0_Channel0_IRQHandler(void)
{
    if(SET == dma_interrupt_flag_get(DMA0, DMA_CH0, DMA_INT_FLAG_FTF))
    {
        dma_interrupt_flag_clear(DMA0, DMA_CH0, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA0_CH0, DRV_DMA_INT_FTF);
    }

    if(SET == dma_interrupt_flag_get(DMA0, DMA_CH0, DMA_INT_FLAG_HTF))
    {
        dma_interrupt_flag_clear(DMA0, DMA_CH0, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA0_CH0, DRV_DMA_INT_HTF);
    }

    if(SET == dma_interrupt_flag_get(DMA0, DMA_CH0, DMA_INT_FLAG_ERR))
    {
        dma_interrupt_flag_clear(DMA0, DMA_CH0, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA0_CH0, DRV_DMA_INT_ERR);
    }
}

/*********************************************************************
 * @brief   DMA0 Channel 1 中断服务函数
 *********************************************************************/
void DMA0_Channel1_IRQHandler(void)
{
    if(SET == dma_interrupt_flag_get(DMA0, DMA_CH1, DMA_INT_FLAG_FTF))
    {
        dma_interrupt_flag_clear(DMA0, DMA_CH1, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA0_CH1, DRV_DMA_INT_FTF);
    }

    if(SET == dma_interrupt_flag_get(DMA0, DMA_CH1, DMA_INT_FLAG_HTF))
    {
        dma_interrupt_flag_clear(DMA0, DMA_CH1, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA0_CH1, DRV_DMA_INT_HTF);
    }

    if(SET == dma_interrupt_flag_get(DMA0, DMA_CH1, DMA_INT_FLAG_ERR))
    {
        dma_interrupt_flag_clear(DMA0, DMA_CH1, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA0_CH1, DRV_DMA_INT_ERR);
    }
}

/*********************************************************************
 * @brief   DMA0 Channel 2 中断服务函数
 *********************************************************************/
void DMA0_Channel2_IRQHandler(void)
{
    if(SET == dma_interrupt_flag_get(DMA0, DMA_CH2, DMA_INT_FLAG_FTF))
    {
        dma_interrupt_flag_clear(DMA0, DMA_CH2, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA0_CH2, DRV_DMA_INT_FTF);
    }

    if(SET == dma_interrupt_flag_get(DMA0, DMA_CH2, DMA_INT_FLAG_HTF))
    {
        dma_interrupt_flag_clear(DMA0, DMA_CH2, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA0_CH2, DRV_DMA_INT_HTF);
    }

    if(SET == dma_interrupt_flag_get(DMA0, DMA_CH2, DMA_INT_FLAG_ERR))
    {
        dma_interrupt_flag_clear(DMA0, DMA_CH2, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA0_CH2, DRV_DMA_INT_ERR);
    }
}

/*********************************************************************
 * @brief   DMA0 Channel 3 中断服务函数
 *********************************************************************/
void DMA0_Channel3_IRQHandler(void)
{
    if(SET == dma_interrupt_flag_get(DMA0, DMA_CH3, DMA_INT_FLAG_FTF))
    {
        dma_interrupt_flag_clear(DMA0, DMA_CH3, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA0_CH3, DRV_DMA_INT_FTF);
    }

    if(SET == dma_interrupt_flag_get(DMA0, DMA_CH3, DMA_INT_FLAG_HTF))
    {
        dma_interrupt_flag_clear(DMA0, DMA_CH3, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA0_CH3, DRV_DMA_INT_HTF);
    }

    if(SET == dma_interrupt_flag_get(DMA0, DMA_CH3, DMA_INT_FLAG_ERR))
    {
        dma_interrupt_flag_clear(DMA0, DMA_CH3, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA0_CH3, DRV_DMA_INT_ERR);
    }
}

/*********************************************************************
 * @brief   DMA0 Channel 4 中断服务函数
 *********************************************************************/
void DMA0_Channel4_IRQHandler(void)
{
    if(SET == dma_interrupt_flag_get(DMA0, DMA_CH4, DMA_INT_FLAG_FTF))
    {
        dma_interrupt_flag_clear(DMA0, DMA_CH4, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA0_CH4, DRV_DMA_INT_FTF);
    }

    if(SET == dma_interrupt_flag_get(DMA0, DMA_CH4, DMA_INT_FLAG_HTF))
    {
        dma_interrupt_flag_clear(DMA0, DMA_CH4, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA0_CH4, DRV_DMA_INT_HTF);
    }

    if(SET == dma_interrupt_flag_get(DMA0, DMA_CH4, DMA_INT_FLAG_ERR))
    {
        dma_interrupt_flag_clear(DMA0, DMA_CH4, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA0_CH4, DRV_DMA_INT_ERR);
    }
}

/*********************************************************************
 * @brief   DMA0 Channel 5 中断服务函数
 *********************************************************************/
void DMA0_Channel5_IRQHandler(void)
{
    if(SET == dma_interrupt_flag_get(DMA0, DMA_CH5, DMA_INT_FLAG_FTF))
    {
        dma_interrupt_flag_clear(DMA0, DMA_CH5, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA0_CH5, DRV_DMA_INT_FTF);
    }

    if(SET == dma_interrupt_flag_get(DMA0, DMA_CH5, DMA_INT_FLAG_HTF))
    {
        dma_interrupt_flag_clear(DMA0, DMA_CH5, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA0_CH5, DRV_DMA_INT_HTF);
    }

    if(SET == dma_interrupt_flag_get(DMA0, DMA_CH5, DMA_INT_FLAG_ERR))
    {
        dma_interrupt_flag_clear(DMA0, DMA_CH5, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA0_CH5, DRV_DMA_INT_ERR);
    }
}

/*********************************************************************
 * @brief   DMA0 Channel 6 中断服务函数
 *********************************************************************/
void DMA0_Channel6_IRQHandler(void)
{
    if(SET == dma_interrupt_flag_get(DMA0, DMA_CH6, DMA_INT_FLAG_FTF))
    {
        dma_interrupt_flag_clear(DMA0, DMA_CH6, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA0_CH6, DRV_DMA_INT_FTF);
    }

    if(SET == dma_interrupt_flag_get(DMA0, DMA_CH6, DMA_INT_FLAG_HTF))
    {
        dma_interrupt_flag_clear(DMA0, DMA_CH6, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA0_CH6, DRV_DMA_INT_HTF);
    }

    if(SET == dma_interrupt_flag_get(DMA0, DMA_CH6, DMA_INT_FLAG_ERR))
    {
        dma_interrupt_flag_clear(DMA0, DMA_CH6, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA0_CH6, DRV_DMA_INT_ERR);
    }
}

/*********************************************************************
 * @brief   DMA1 Channel 0 中断服务函数
 *********************************************************************/
void DMA1_Channel0_IRQHandler(void)
{
    if(SET == dma_interrupt_flag_get(DMA1, DMA_CH0, DMA_INT_FLAG_FTF))
    {
        dma_interrupt_flag_clear(DMA1, DMA_CH0, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA1_CH0, DRV_DMA_INT_FTF);
    }

    if(SET == dma_interrupt_flag_get(DMA1, DMA_CH0, DMA_INT_FLAG_HTF))
    {
        dma_interrupt_flag_clear(DMA1, DMA_CH0, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA1_CH0, DRV_DMA_INT_HTF);
    }

    if(SET == dma_interrupt_flag_get(DMA1, DMA_CH0, DMA_INT_FLAG_ERR))
    {
        dma_interrupt_flag_clear(DMA1, DMA_CH0, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA1_CH0, DRV_DMA_INT_ERR);
    }
}

/*********************************************************************
 * @brief   DMA1 Channel 1 中断服务函数
 *********************************************************************/
void DMA1_Channel1_IRQHandler(void)
{
    if(SET == dma_interrupt_flag_get(DMA1, DMA_CH1, DMA_INT_FLAG_FTF))
    {
        dma_interrupt_flag_clear(DMA1, DMA_CH1, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA1_CH1, DRV_DMA_INT_FTF);
    }

    if(SET == dma_interrupt_flag_get(DMA1, DMA_CH1, DMA_INT_FLAG_HTF))
    {
        dma_interrupt_flag_clear(DMA1, DMA_CH1, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA1_CH1, DRV_DMA_INT_HTF);
    }

    if(SET == dma_interrupt_flag_get(DMA1, DMA_CH1, DMA_INT_FLAG_ERR))
    {
        dma_interrupt_flag_clear(DMA1, DMA_CH1, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA1_CH1, DRV_DMA_INT_ERR);
    }
}

/*********************************************************************
 * @brief   DMA1 Channel 2 中断服务函数
 *********************************************************************/
void DMA1_Channel2_IRQHandler(void)
{
    if(SET == dma_interrupt_flag_get(DMA1, DMA_CH2, DMA_INT_FLAG_FTF))
    {
        dma_interrupt_flag_clear(DMA1, DMA_CH2, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA1_CH2, DRV_DMA_INT_FTF);
    }

    if(SET == dma_interrupt_flag_get(DMA1, DMA_CH2, DMA_INT_FLAG_HTF))
    {
        dma_interrupt_flag_clear(DMA1, DMA_CH2, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA1_CH2, DRV_DMA_INT_HTF);
    }

    if(SET == dma_interrupt_flag_get(DMA1, DMA_CH2, DMA_INT_FLAG_ERR))
    {
        dma_interrupt_flag_clear(DMA1, DMA_CH2, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA1_CH2, DRV_DMA_INT_ERR);
    }
}

/*********************************************************************
 * @brief   DMA1 Channel 3 中断服务函数
 *********************************************************************/
void DMA1_Channel3_IRQHandler(void)
{
    if(SET == dma_interrupt_flag_get(DMA1, DMA_CH3, DMA_INT_FLAG_FTF))
    {
        dma_interrupt_flag_clear(DMA1, DMA_CH3, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA1_CH3, DRV_DMA_INT_FTF);
    }

    if(SET == dma_interrupt_flag_get(DMA1, DMA_CH3, DMA_INT_FLAG_HTF))
    {
        dma_interrupt_flag_clear(DMA1, DMA_CH3, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA1_CH3, DRV_DMA_INT_HTF);
    }

    if(SET == dma_interrupt_flag_get(DMA1, DMA_CH3, DMA_INT_FLAG_ERR))
    {
        dma_interrupt_flag_clear(DMA1, DMA_CH3, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA1_CH3, DRV_DMA_INT_ERR);
    }
}

/*********************************************************************
 * @brief   DMA1 Channel 4 中断服务函数
 *********************************************************************/
void DMA1_Channel4_IRQHandler(void)
{
    if(SET == dma_interrupt_flag_get(DMA1, DMA_CH4, DMA_INT_FLAG_FTF))
    {
        dma_interrupt_flag_clear(DMA1, DMA_CH4, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA1_CH4, DRV_DMA_INT_FTF);
    }

    if(SET == dma_interrupt_flag_get(DMA1, DMA_CH4, DMA_INT_FLAG_HTF))
    {
        dma_interrupt_flag_clear(DMA1, DMA_CH4, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA1_CH4, DRV_DMA_INT_HTF);
    }

    if(SET == dma_interrupt_flag_get(DMA1, DMA_CH4, DMA_INT_FLAG_ERR))
    {
        dma_interrupt_flag_clear(DMA1, DMA_CH4, DMA_INT_FLAG_GIF);
        drv_dma_run_callback(DRV_DMA1_CH4, DRV_DMA_INT_ERR);
    }
}

/*********************************************************************
 * UART 中断服务函数
 *
 * 设计说明：
 *   1. 每个 UART 端口独立实现 ISR，调用 drv_uart_irq_handler 统一处理
 *   2. 遵循 GD32 官方示例规范，使用官方 IRQ Handler 名称
 *   3. 中断处理逻辑在驱动层实现，ISR 仅负责转发
 *********************************************************************/

/*********************************************************************
 * @brief   USART0 中断服务函数
 * @note    由硬件自动调用，转发到驱动层统一处理
 *********************************************************************/
void USART0_IRQHandler(void)
{
    drv_uart_irq_handler(DRV_UART_PORT_USART0);
}

/*********************************************************************
 * @brief   USART1 中断服务函数
 * @note    由硬件自动调用，转发到驱动层统一处理
 *********************************************************************/
void USART1_IRQHandler(void)
{
    drv_uart_irq_handler(DRV_UART_PORT_USART1);
}

/*********************************************************************
 * @brief   USART2 中断服务函数
 * @note    由硬件自动调用，转发到驱动层统一处理
 *********************************************************************/
void USART2_IRQHandler(void)
{
    drv_uart_irq_handler(DRV_UART_PORT_USART2);
}

/*********************************************************************
 * @brief   UART3 中断服务函数
 * @note    由硬件自动调用，转发到驱动层统一处理
 *********************************************************************/
void UART3_IRQHandler(void)
{
    drv_uart_irq_handler(DRV_UART_PORT_UART3);
}

/*********************************************************************
 * @brief   UART4 中断服务函数
 * @note    由硬件自动调用，转发到驱动层统一处理
 *********************************************************************/
void UART4_IRQHandler(void)
{
    drv_uart_irq_handler(DRV_UART_PORT_UART4);
}

/*********************************************************************
 * ADC 中断服务函数
 *
 * 设计说明：
 *   1. ADC0/1共享ADC0_1_IRQn中断
 *   2. ADC2独立ADC2_IRQn中断
 *   3. ISR调用drv_adc_irq_handler()统一处理
 *   4. 仅处理看门狗（AWD）中断，用于低功耗唤醒
 *   5. EOC/EIC中断不使用，避免频繁中断影响系统性能
 *********************************************************************/

/*********************************************************************
 * @brief   ADC0/1 中断服务函数
 * @note    由硬件自动调用，转发到驱动层统一处理
 *********************************************************************/
void ADC0_1_IRQHandler(void)
{
    drv_adc_irq_handler(DRV_ADC0);
    drv_adc_irq_handler(DRV_ADC1);
}

/*********************************************************************
 * @brief   ADC2 中断服务函数
 * @note    由硬件自动调用，转发到驱动层统一处理
 *********************************************************************/
void ADC2_IRQHandler(void)
{
    drv_adc_irq_handler(DRV_ADC2);
}

/*********************************************************************
 * EXTI 中断服务函数
 *
 * 设计说明：
 *   1. EXTI0~4独立中断线，各自独立ISR
 *   2. EXTI5~9共享EXTI5_9_IRQn
 *   3. EXTI10~15共享EXTI10_15_IRQn
 *   4. ISR调用drv_gpio_exti_handler()统一处理
 *   5. 驱动层自动查找并执行注册的回调函数
 *********************************************************************/

/*********************************************************************
 * @brief   EXTI0 中断服务函数（PB0）
 * @note    由硬件自动调用，转发到驱动层统一处理
 *********************************************************************/
void EXTI0_IRQHandler(void)
{
    if (SET == exti_interrupt_flag_get(EXTI_0))
    {
        exti_interrupt_flag_clear(EXTI_0);
        drv_gpio_exti_handler(EXTI_0);
    }
}

/*********************************************************************
 * @brief   EXTI1 中断服务函数
 * @note    由硬件自动调用，转发到驱动层统一处理
 *********************************************************************/
void EXTI1_IRQHandler(void)
{
    if (SET == exti_interrupt_flag_get(EXTI_1))
    {
        exti_interrupt_flag_clear(EXTI_1);
        drv_gpio_exti_handler(EXTI_1);
    }
}

/*********************************************************************
 * @brief   EXTI2 中断服务函数
 * @note    由硬件自动调用，转发到驱动层统一处理
 *********************************************************************/
void EXTI2_IRQHandler(void)
{
    if (SET == exti_interrupt_flag_get(EXTI_2))
    {
        exti_interrupt_flag_clear(EXTI_2);
        drv_gpio_exti_handler(EXTI_2);
    }
}

/*********************************************************************
 * @brief   EXTI3 中断服务函数（PB3）
 * @note    由硬件自动调用，转发到驱动层统一处理
 *********************************************************************/
void EXTI3_IRQHandler(void)
{
    if (SET == exti_interrupt_flag_get(EXTI_3))
    {
        exti_interrupt_flag_clear(EXTI_3);
        drv_gpio_exti_handler(EXTI_3);
    }
}

/*********************************************************************
 * @brief   EXTI4 中断服务函数（PB4）
 * @note    由硬件自动调用，转发到驱动层统一处理
 *********************************************************************/
void EXTI4_IRQHandler(void)
{
    if (SET == exti_interrupt_flag_get(EXTI_4))
    {
        exti_interrupt_flag_clear(EXTI_4);
        drv_gpio_exti_handler(EXTI_4);
    }
}

/*********************************************************************
 * @brief   EXTI5~9 中断服务函数
 * @note    由硬件自动调用，转发到驱动层统一处理
 *********************************************************************/
void EXTI5_9_IRQHandler(void)
{
    /* 检查EXTI5~9哪个中断标志置位 */
    if(SET == exti_interrupt_flag_get(EXTI_5))
    {
        exti_interrupt_flag_clear(EXTI_5);
        drv_gpio_exti_handler(EXTI_5);
    }
    if(SET == exti_interrupt_flag_get(EXTI_6))
    {
        exti_interrupt_flag_clear(EXTI_6);
        drv_gpio_exti_handler(EXTI_6);
    }
    if(SET == exti_interrupt_flag_get(EXTI_7))
    {
        exti_interrupt_flag_clear(EXTI_7);
        drv_gpio_exti_handler(EXTI_7);
    }
    if(SET == exti_interrupt_flag_get(EXTI_8))
    {
        exti_interrupt_flag_clear(EXTI_8);
        drv_gpio_exti_handler(EXTI_8);
    }
    if(SET == exti_interrupt_flag_get(EXTI_9))
    {
        exti_interrupt_flag_clear(EXTI_9);
        drv_gpio_exti_handler(EXTI_9);
    }
}

/*********************************************************************
 * @brief   EXTI10~15 中断服务函数（PB14在此范围）
 * @note    由硬件自动调用，转发到驱动层统一处理
 *********************************************************************/
void EXTI10_15_IRQHandler(void)
{
    /* 检查EXTI10~15哪个中断标志置位 */
    if(SET == exti_interrupt_flag_get(EXTI_10))
    {
        exti_interrupt_flag_clear(EXTI_10);
        drv_gpio_exti_handler(EXTI_10);
    }
    if(SET == exti_interrupt_flag_get(EXTI_11))
    {
        exti_interrupt_flag_clear(EXTI_11);
        drv_gpio_exti_handler(EXTI_11);
    }
    if(SET == exti_interrupt_flag_get(EXTI_12))
    {
        exti_interrupt_flag_clear(EXTI_12);
        drv_gpio_exti_handler(EXTI_12);
    }
    if(SET == exti_interrupt_flag_get(EXTI_13))
    {
        exti_interrupt_flag_clear(EXTI_13);
        drv_gpio_exti_handler(EXTI_13);
    }
    if(SET == exti_interrupt_flag_get(EXTI_14))
    {
        exti_interrupt_flag_clear(EXTI_14);
        drv_gpio_exti_handler(EXTI_14);
    }
    if(SET == exti_interrupt_flag_get(EXTI_15))
    {
        exti_interrupt_flag_clear(EXTI_15);
        drv_gpio_exti_handler(EXTI_15);
    }
}

/*********************************************************************
 * CAN 中断服务函数
 *
 * 设计说明：
 *   1. CAN0/CAN1各有4个中断：TX/RX0/RX1/EWMC
 *   2. RX中断检查FIFO非空中断标志，读取消息后执行回调
 *   3. TX中断检查邮箱空中断标志，执行发送完成回调
 *   4. EWMC中断处理错误和总线关闭事件
 *   5. 保持ISR快速执行，复杂处理交给回调函数
 *********************************************************************/

/*********************************************************************
 * @brief   CAN0 RX0 中断服务函数
 * @note    处理FIFO0接收消息
 *********************************************************************/
void CAN0_RX0_IRQHandler(void)
{
    drv_can_frame_t frame;

    /* 检查FIFO0非空中断标志 */
    if (SET == can_interrupt_flag_get(CAN0, CAN_INT_FLAG_RFL0))
    {
        /* 读取消息（使用无锁版本，避免ISR中死锁） */
        if (_drv_can_receive_no_lock(DRV_CAN_PORT_CAN0, &frame, 0) == DRV_CAN_ERR_OK)
        {
            /* 执行接收回调 */
            drv_can_run_rx_callback(DRV_CAN_PORT_CAN0, &frame, 0);
        }
    }
}

/*********************************************************************
 * @brief   CAN0 RX1 中断服务函数
 * @note    处理FIFO1接收消息
 *********************************************************************/
void CAN0_RX1_IRQHandler(void)
{
    drv_can_frame_t frame;

    /* 检查FIFO1非空中断标志 */
    if (SET == can_interrupt_flag_get(CAN0, CAN_INT_FLAG_RFL1))
    {
        /* 读取消息（使用无锁版本，避免ISR中死锁） */
        if (_drv_can_receive_no_lock(DRV_CAN_PORT_CAN0, &frame, 1) == DRV_CAN_ERR_OK)
        {
            /* 执行接收回调 */
            drv_can_run_rx_callback(DRV_CAN_PORT_CAN0, &frame, 1);
        }
    }
}

/*********************************************************************
 * @brief   CAN0 TX 中断服务函数
 * @note    处理发送邮箱空中断
 *********************************************************************/
void CAN0_TX_IRQHandler(void)
{
    uint8_t mailbox;

    /* 检查3个发送邮箱 */
    for (mailbox = 0; mailbox < 3; mailbox++)
    {
        if (SET == can_interrupt_flag_get(CAN0, CAN_INT_FLAG_MTF0 + mailbox))
        {
            /* 执行发送完成回调 */
            drv_can_run_tx_callback(DRV_CAN_PORT_CAN0, mailbox);
        }
    }
}

/*********************************************************************
 * @brief   CAN0 EWMC 中断服务函数
 * @note    处理错误和警告事件
 *********************************************************************/
void CAN0_EWMC_IRQHandler(void)
{
    drv_can_err_type_e err_type = DRV_CAN_ERR_TYPE_NONE;

    /* 检查错误中断标志 */
    if (SET == can_interrupt_flag_get(CAN0, CAN_INT_FLAG_ERRIF))
    {
        /* 判断错误类型 */
        if (SET == can_flag_get(CAN0, CAN_FLAG_BOERR))
        {
            /* 总线关闭错误 */
            err_type = DRV_CAN_ERR_TYPE_BIT;  /* BOERR表示Bus Off，归类为位错误 */
        }
        else if (SET == can_flag_get(CAN0, CAN_FLAG_PERR))
        {
            err_type = DRV_CAN_ERR_TYPE_ACK;  /* PERR表示Error Passive，通常是ACK错误导致 */
        }
        else if (SET == can_flag_get(CAN0, CAN_FLAG_WERR))
        {
            err_type = DRV_CAN_ERR_TYPE_STUFF;  /* WERR表示Warning，通常是填充错误早期预警 */
        }

        /* 执行错误回调 */
        if (err_type != DRV_CAN_ERR_TYPE_NONE)
        {
            drv_can_run_err_callback(DRV_CAN_PORT_CAN0, err_type);
        }

        /* 清除错误标志 */
        can_flag_clear(CAN0, CAN_FLAG_BOERR | CAN_FLAG_PERR | CAN_FLAG_WERR);
    }
}

/*********************************************************************
 * @brief   CAN1 RX0 中断服务函数
 * @note    处理FIFO0接收消息
 *********************************************************************/
void CAN1_RX0_IRQHandler(void)
{
    drv_can_frame_t frame;

    /* 检查FIFO0非空中断标志 */
    if (SET == can_interrupt_flag_get(CAN1, CAN_INT_FLAG_RFL0))
    {
        /* 读取消息（使用无锁版本，避免ISR中死锁） */
        if (_drv_can_receive_no_lock(DRV_CAN_PORT_CAN1, &frame, 0) == DRV_CAN_ERR_OK)
        {
            /* 执行接收回调 */
            drv_can_run_rx_callback(DRV_CAN_PORT_CAN1, &frame, 0);
        }
    }
}

/*********************************************************************
 * @brief   CAN1 RX1 中断服务函数
 * @note    处理FIFO1接收消息
 *********************************************************************/
void CAN1_RX1_IRQHandler(void)
{
    drv_can_frame_t frame;

    /* 检查FIFO1非空中断标志 */
    if (SET == can_interrupt_flag_get(CAN1, CAN_INT_FLAG_RFL1))
    {
        /* 读取消息（使用无锁版本，避免ISR中死锁） */
        if (_drv_can_receive_no_lock(DRV_CAN_PORT_CAN1, &frame, 1) == DRV_CAN_ERR_OK)
        {
            /* 执行接收回调 */
            drv_can_run_rx_callback(DRV_CAN_PORT_CAN1, &frame, 1);
        }
    }
}

/*********************************************************************
 * @brief   CAN1 TX 中断服务函数
 * @note    处理发送邮箱空中断
 *********************************************************************/
void CAN1_TX_IRQHandler(void)
{
    uint8_t mailbox;

    /* 检查3个发送邮箱 */
    for (mailbox = 0; mailbox < 3; mailbox++)
    {
        if (SET == can_interrupt_flag_get(CAN1, CAN_INT_FLAG_MTF0 + mailbox))
        {
            /* 执行发送完成回调 */
            drv_can_run_tx_callback(DRV_CAN_PORT_CAN1, mailbox);
        }
    }
}

/*********************************************************************
 * @brief   CAN1 EWMC 中断服务函数
 * @note    处理错误和警告事件
 *********************************************************************/
void CAN1_EWMC_IRQHandler(void)
{
    drv_can_err_type_e err_type = DRV_CAN_ERR_TYPE_NONE;

    /* 检查错误中断标志 */
    if (SET == can_interrupt_flag_get(CAN1, CAN_INT_FLAG_ERRIF))
    {
        /* 判断错误类型 */
        if (SET == can_flag_get(CAN1, CAN_FLAG_BOERR))
        {
            /* 总线关闭错误 */
            err_type = DRV_CAN_ERR_TYPE_BIT;  /* BOERR表示Bus Off，归类为位错误 */
        }
        else if (SET == can_flag_get(CAN1, CAN_FLAG_PERR))
        {
            err_type = DRV_CAN_ERR_TYPE_ACK;  /* PERR表示Error Passive，通常是ACK错误导致 */
        }
        else if (SET == can_flag_get(CAN1, CAN_FLAG_WERR))
        {
            err_type = DRV_CAN_ERR_TYPE_STUFF;  /* WERR表示Warning，通常是填充错误早期预警 */
        }

        /* 执行错误回调 */
        if (err_type != DRV_CAN_ERR_TYPE_NONE)
        {
            drv_can_run_err_callback(DRV_CAN_PORT_CAN1, err_type);
        }

        /* 清除错误标志 */
        can_flag_clear(CAN1, CAN_FLAG_BOERR | CAN_FLAG_PERR | CAN_FLAG_WERR);
    }
}

/*********************************************************************
 * @brief   RTC 中断服务函数
 * @note    秒中断、闹钟中断等统一由 drv_rtc_irq_handler 分发
 *********************************************************************/
void RTC_IRQHandler(void)
{
    drv_rtc_irq_handler();
}
