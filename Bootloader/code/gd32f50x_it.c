/*!
    \file    gd32f50x_it.c
    \brief   interrupt service routines

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

#include "gd32f50x_it.h"
#include "systick.h"
#include "my_bl_log.h"

#define SRAM_ECC_ERROR_HANDLE(s)    do{}while(1)

/*********************************************************************
 * HardFault 诊断处理函数（由裸函数调用，传入正确的栈帧指针）
 *********************************************************************/
void hardfault_dump(uint32_t *sp_frame);

/*********************************************************************
 * @brief   HardFault 异常处理函数（naked wrapper）
 * @note    使用 naked 属性避免编译器压栈破坏 MSP，确保 sp_frame 指向真正的异常栈帧
 *********************************************************************/
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

/*********************************************************************
 * @brief   HardFault 诊断处理函数
 * @param   sp_frame: 异常栈帧指针 (R0-R3, R12, LR, PC, xPSR)
 * @note    此时 sp_frame 指向 CPU 自动压栈的真正栈帧
 *********************************************************************/
void hardfault_dump(uint32_t *sp_frame)
{
    uint32_t cfsr = SCB->CFSR;
    uint32_t hfsr = SCB->HFSR;
    uint32_t mmfar = SCB->MMFAR;
    uint32_t bfar = SCB->BFAR;
    volatile uint32_t i;

    /* 重新初始化日志系统 */
    my_bl_log_init();

    MY_LOG_I("===== BOOT HARDFAULT =====");
    MY_LOG_I("CFSR  = 0x%08X", cfsr);
    MY_LOG_I("HFSR  = 0x%08X", hfsr);
    MY_LOG_I("MMFAR = 0x%08X", mmfar);
    MY_LOG_I("BFAR  = 0x%08X", bfar);

    /* CFSR 详细解析 (MMFSR[7:0] + BFSR[15:8] + UFSR[31:16]) */
    /* MMFSR (MemManage Fault Status Register) */
    if (cfsr & SCB_CFSR_IACCVIOL_Msk)     MY_LOG_I("  ->  IACCVIOL: Instruction access violation");
    if (cfsr & SCB_CFSR_DACCVIOL_Msk)     MY_LOG_I("  ->  DACCVIOL: Data access violation");
    if (cfsr & SCB_CFSR_MUNSTKERR_Msk)    MY_LOG_I("  ->  MUNSTKERR: MemManage on unstacking");
    if (cfsr & SCB_CFSR_MSTKERR_Msk)      MY_LOG_I("  ->  MSTKERR: MemManage on stacking");
    if (cfsr & SCB_CFSR_MLSPERR_Msk)      MY_LOG_I("  ->  MLSPERR: MemManage lazy save error");
    if (cfsr & SCB_CFSR_MMARVALID_Msk)    MY_LOG_I("  ->  MMARVALID: MMFAR address valid");

    /* BFSR (BusFault Status Register) */
    if (cfsr & SCB_CFSR_IBUSERR_Msk)      MY_LOG_I("  ->  IBUSERR: Instruction bus error");
    if (cfsr & SCB_CFSR_PRECISERR_Msk)    MY_LOG_I("  ->  PRECISERR: Precise data bus error");
    if (cfsr & SCB_CFSR_IMPRECISERR_Msk)  MY_LOG_I("  ->  IMPRECISERR: Imprecise data bus error");
    if (cfsr & SCB_CFSR_UNSTKERR_Msk)     MY_LOG_I("  ->  UNSTKERR: Bus fault on unstacking");
    if (cfsr & SCB_CFSR_STKERR_Msk)       MY_LOG_I("  ->  STKERR: Bus fault on stacking");
    if (cfsr & SCB_CFSR_LSPERR_Msk)       MY_LOG_I("  ->  LSPERR: Bus lazy save error");
    if (cfsr & SCB_CFSR_BFARVALID_Msk)    MY_LOG_I("  ->  BFARVALID: BFAR address valid");

     /* UFSR (UsageFault Status Register) */
    if (cfsr & SCB_CFSR_UNDEFINSTR_Msk)   MY_LOG_I("  ->  UNDEFINSTR: Undefined instruction");
    if (cfsr & SCB_CFSR_INVSTATE_Msk)     MY_LOG_I("  ->  INVSTATE: Invalid state");
    if (cfsr & SCB_CFSR_INVPC_Msk)        MY_LOG_I("  ->  INVPC: Invalid PC load");
    if (cfsr & SCB_CFSR_NOCP_Msk)         MY_LOG_I("  ->  NOCP: No coprocessor");
    if (cfsr & SCB_CFSR_STKOF_Msk)        MY_LOG_I("  ->  STKOF: Stack overflow");
    if (cfsr & SCB_CFSR_UNALIGNED_Msk)    MY_LOG_I("  ->  UNALIGNED: Unaligned access");
    if (cfsr & SCB_CFSR_DIVBYZERO_Msk)    MY_LOG_I("  ->  DIVBYZERO: Divide by zero");

    /* HFSR 解析 */
    if (hfsr & SCB_HFSR_VECTTBL_Msk)     MY_LOG_I("  ->  VECTTBL: Vector table read error");
    if (hfsr & SCB_HFSR_FORCED_Msk)      MY_LOG_I("  ->  FORCED: Forced hard fault (escalated)");
    if (hfsr & SCB_HFSR_DEBUGEVT_Msk)    MY_LOG_I("  ->  DEBUGEVT: Debug event occurred");

    /* 栈帧寄存器（sp_frame 指向真正的异常栈帧） */
    MY_LOG_I("===== Stack Frame =====");
    MY_LOG_I("R0  = 0x%08X", sp_frame[0]);
    MY_LOG_I("R1  = 0x%08X", sp_frame[1]);
    MY_LOG_I("R2  = 0x%08X", sp_frame[2]);
    MY_LOG_I("R3  = 0x%08X", sp_frame[3]);
    MY_LOG_I("R12 = 0x%08X", sp_frame[4]);
    MY_LOG_I("LR  = 0x%08X", sp_frame[5]);
    MY_LOG_I("PC  = 0x%08X", sp_frame[6]);
    MY_LOG_I("xPSR= 0x%08X", sp_frame[7]);
    MY_LOG_I("MSP = 0x%08X", __get_MSP());
    MY_LOG_I("PSP = 0x%08X", __get_PSP());
    MY_LOG_I("===== END =====\n");

    /* 等待日志输出完成后复位系统 */
    for (i = 0U; i < 5000000U; i++) {}
    NVIC_SystemReset();

    while (1U) { }
}

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
        /* HXTAL clock monitor NMI error or NMI pin error */
        while(1) {
        }
    }
}

/*********************************************************************
 * @brief   MemManage 异常处理函数
 *********************************************************************/
void MemManage_Handler(void)
{
    uint32_t cfsr = SCB->CFSR;
    uint32_t mmfar = SCB->MMFAR;
    volatile uint32_t i;

    my_bl_log_init();

    MY_LOG_I("===== BOOT MEMMANAGE FAULT =====");
    MY_LOG_I("CFSR  = 0x%08X", cfsr);
    MY_LOG_I("MMFAR = 0x%08X", mmfar);

    /* 等待日志输出完成后复位系统 */
    for (i = 0U; i < 5000000U; i++) {}
    NVIC_SystemReset();

    while (1U) { }
}

/*********************************************************************
 * @brief   BusFault 异常处理函数
 *********************************************************************/
void BusFault_Handler(void)
{
    uint32_t cfsr = SCB->CFSR;
    uint32_t bfar = SCB->BFAR;
    volatile uint32_t i;

    my_bl_log_init();

    MY_LOG_I("===== BOOT BUSFAULT =====");
    MY_LOG_I("CFSR = 0x%08X", cfsr);
    MY_LOG_I("BFAR = 0x%08X", bfar);

    /* 等待日志输出完成后复位系统 */
    for (i = 0U; i < 5000000U; i++) {}
    NVIC_SystemReset();

    while (1U) { }
}

/*********************************************************************
 * @brief   UsageFault 异常处理函数
 *********************************************************************/
void UsageFault_Handler(void)
{
    uint32_t cfsr = SCB->CFSR;
    volatile uint32_t d;

    my_bl_log_init();

    MY_LOG_I("===== BOOT USAGEFAULT =====");
    MY_LOG_I("CFSR = 0x%08X", cfsr);

    /* 等待日志输出完成后复位系统 */
    for (d = 0U; d < 5000000U; d++) {}
    NVIC_SystemReset();

    while (1U) { }
}

/*!
    \brief      this function handles SVC exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
void SVC_Handler(void)
{
    /* if SVC exception occurs, go to infinite loop */
    while(1) {
    }
}

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
*/
void PendSV_Handler(void)
{
    /* if PendSV exception occurs, go to infinite loop */
    while(1) {
    }
}

/*!
    \brief      this function handles SysTick exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
void SysTick_Handler(void)
{
    my_bl_systick_handler();
}
