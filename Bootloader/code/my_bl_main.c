/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       my_bl_main.c
**文件描述：       Bootloader主入口文件
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.06.22
*********************************************************************
** 功能描述：       1. 系统初始化（时钟、日志）
**                 2. bootconf读取与OTA升级判断
**                 3. APP有效性校验与跳转
*********************************************************************/

#include "my_bl.h"

/*********************************************************************
 * APP 入口地址定义
 *********************************************************************/
/* 栈指针有效性检查（GD32F505VGT7 SRAM: 0x20000000~0x20030000, 192KB） */
#define SRAM_BASE_ADDR          SRAM_BASE              /* 栈指针起始地址 0x20000000 */
#define SRAM_END_ADDR           (SRAM_BASE + 0x30000)  /* SRAM: 0x20000000~0x20030000, 192KB */

/*********************************************************************
 * 内部函数声明
 *********************************************************************/
typedef void (*app_func)(void);

/*********************************************************************
 * @brief   跳转到 APP 应用程序
 * @param   None
 * @return  None
 * @note    Cortex-M Bootloader 跳转最佳实践：
 *          1. 跳转顺序：读取APP入口地址 → 设置VTOR → __DSB() → 设置MSP → 跳转
 *          2. 必须先读取APP入口地址再设置VTOR，确保在Bootloader向量表上下文中读取
 *          3. VTOR设置后必须插入__DSB()内存屏障，确保向量表切换完成后再操作
 *          4. 禁止调用__disable_irq()，Cortex-M33上PRIMASK会导致栈/向量切换时序异常
 *          5. 跳转前需关闭SysTick、停用Bootloader使用的外设（UART/FLASH等）、清除NVIC所有中断使能和挂起状态
 *          6. 跳转前必须验证APP栈指针是否在合法SRAM范围内
 *********************************************************************/
void jump_to_execute(void)
{
    app_func application;
    uint32_t app_address;
    uint32_t sram_sect = REG32(BL_FLASH_APP_BASE);

    /* 验证APP栈指针合法性 */
    if((sram_sect >= SRAM_BASE_ADDR) && (sram_sect < SRAM_END_ADDR))
    {
        /* 关闭SysTick */
        SysTick->CTRL = 0U;
        SysTick->LOAD = 0U;
        SysTick->VAL  = 0U;

        /* 停用Bootloader使用的外设（如UART、FLASH等） */
        /* TODO: 根据实际使用的外设添加停用代码 */

        /* 清除NVIC所有中断使能和挂起状态 */
        for (int i = 0; i < 8; i++)
        {
            NVIC->ICER[i] = 0xFFFFFFFFU;
            NVIC->ICPR[i] = 0xFFFFFFFFU;
        }

        /* 读取APP入口地址（在Bootloader向量表上下文中） */
        app_address = *(__IO uint32_t*) (BL_FLASH_APP_BASE + 4U);
        application = (app_func) app_address;

        /* 切换向量表并确保生效 */
        SCB->VTOR = BL_FLASH_APP_BASE;
        __DSB();

        /* 初始化APP堆栈指针 */
        __set_MSP(*(__IO uint32_t*) BL_FLASH_APP_BASE);

        /* 跳转到APP */
        application();
    }
    else
    {
        MY_LOG_E("invalid APP stack pointer 0x%08X", sram_sect);
    }
}

/*********************************************************************
 * @brief   Bootloader 主函数
 * @param   None
 * @return  int 返回值（不应到达）
 *********************************************************************/
int main(void)
{
    int ret;
    app_func application;
    uint32_t app_address;
    my_bl_bootconf_t bconf;

    /* 1. 硬件初始化 */
    SystemCoreClockUpdate();
    systick_config();

    my_bl_log_init();

    MY_LOG_I("==============================");
    MY_LOG_I("Bootloader:v%s %s %s", BL_VERSION_STRING, BL_BUILD_DATE, BL_BUILD_TIME);
    MY_LOG_I("==============================");

    /* 2. 读取bootconf */
    ret = my_bl_bootconf_read(&bconf);

    if (ret != BL_OK)
    {
        /* 临时调试：打印从 Flash 读到的原始 bootconf 数据 */
        {
            uint8_t raw_buf[64];
            my_bl_flash_read(BL_FLASH_BOOTCONF_BASE, raw_buf, 64);
            MY_LOG_E("bootconf invalid (%d), dumping raw data:", ret);
            for (int i = 0; i < 64; i += 16)
            {
                MY_LOG_E("  %02X: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                    i,
                    raw_buf[i], raw_buf[i+1], raw_buf[i+2], raw_buf[i+3],
                    raw_buf[i+4], raw_buf[i+5], raw_buf[i+6], raw_buf[i+7],
                    raw_buf[i+8], raw_buf[i+9], raw_buf[i+10], raw_buf[i+11],
                    raw_buf[i+12], raw_buf[i+13], raw_buf[i+14], raw_buf[i+15]);
            }
        }

        MY_LOG_E("bootconf invalid (%d), init default", ret);
        my_bl_bootconf_init_default(&bconf);
        my_bl_bootconf_write(&bconf);
    }
    else
    {
        MY_LOG_I("bootconf OK");
    }

    /* 3. 检查OTA标志 */
    if (bconf.ota_flag == BL_OTA_FLAG_PENDING)
    {
        MY_LOG_I("OTA upgrade pending, file_id=0x%02X%02X%02X%02X, size=%u, crc16=0x%04X",
                 bconf.file_id[0], bconf.file_id[1], bconf.file_id[2], bconf.file_id[3],
                 bconf.file_size, bconf.file_crc16);
        MY_LOG_I("OTA MD5: %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
                 bconf.file_md5[0], bconf.file_md5[1], bconf.file_md5[2], bconf.file_md5[3],
                 bconf.file_md5[4], bconf.file_md5[5], bconf.file_md5[6], bconf.file_md5[7],
                 bconf.file_md5[8], bconf.file_md5[9], bconf.file_md5[10], bconf.file_md5[11],
                 bconf.file_md5[12], bconf.file_md5[13], bconf.file_md5[14], bconf.file_md5[15]);

        bconf.last_boot_status = BL_BOOT_STATUS_UPGRADING;
        my_bl_bootconf_write(&bconf);

        ret = my_bl_ota_upgrade(&bconf);

        if (ret == BL_OK)
        {
            MY_LOG_I("OTA success");
            bconf.ota_flag = BL_OTA_FLAG_NONE;
            bconf.last_boot_status = BL_BOOT_STATUS_UPGRADED;
            my_bl_bootconf_write(&bconf);
            // my_bl_system_reset();
        }
        else
        {
            MY_LOG_E("OTA failed: %d", ret);
            bconf.ota_flag = BL_OTA_FLAG_FAILED;
            my_bl_bootconf_write(&bconf);
            /* 继续跳转APP（旧固件仍可运行） */
        }
    }

    MY_LOG_I("Jumping to APP @ 0x%08X...", BL_FLASH_APP_BASE);
    MY_LOG_I("==============================\n\n");

    my_bl_delay_ms(1000);    /* 延时1000ms, 等待RTT被J-Link扫描到并输出 */

    /* 4. 跳转到APP */
    jump_to_execute();

    /* 5. 应用程序永远不会返回 */
    while (1U) {}

    return 0;
}
