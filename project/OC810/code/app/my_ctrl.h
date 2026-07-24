/********************************************************************
**版权所有：         深圳市几米物联有限公司
**文件名称：        my_ctrl.h
**文件描述：        控制任务 - 公共接口与状态定义
**当前版本：        V1.0
**作    者：        伍玉蛟 (wuyujiao@jimiiot.com)
**完成日期：        2026.06.08
*********************************************************************
** 说明：
**   提供CTRL模块的公共接口：
**   - 输出/输入枚举定义（ctrl_out_id_e、ctrl_in_id_e）
**   - 控制状态结构体（输入状态、ADC电压值）
**   - 任务初始化API
**   - 输出控制统一入口（my_ctrl_set_output）
**   消息ID定义于 my_comm.h
*********************************************************************/

#ifndef __MY_CTRL_H__
#define __MY_CTRL_H__

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 *  数据结构定义
 *===========================================================================*/

/**
  * @brief  控制模块输出ID
  * @note   11路输出，供各功能模块通过 ctrl_set_output() 控制
  */
typedef enum {
    CTRL_OUT_IO1 = 0,               /* PE2  通用输出1 */
    CTRL_OUT_IO2,                   /* PE3  通用输出2 */
    CTRL_OUT_PWR_SYS5V,             /* PD5  系统5V，高电平开 */
    CTRL_OUT_PWR_VCC3V3,            /* PD4  3.3V，低电平开 */
    CTRL_OUT_PWR_12V,               /* PE5  12V，高电平开 */
    CTRL_OUT_PWR_5V,                /* PD10 5V，高电平开 */
    CTRL_OUT_LED_REC,               /* PD11 录像状态灯 */
    CTRL_OUT_LED_SYS,               /* PD12 系统状态灯 */
    CTRL_OUT_LED_GNSS,              /* PD13 定位状态灯 */
    CTRL_OUT_LED_NET,               /* PD14 网络状态灯 */
    CTRL_OUT_BUZZER,                /* PA1  蜂鸣器 */
    CTRL_OUT_MAX
} ctrl_out_id_e;

/**
  * @brief  控制模块输入ID
  * @note   数字量输入（10路）；枚举值 0~9 与 @ref ctrl_input_t 的位域 bit 位置一一对应，
  *         操作 s_input_list[] 和位掩码 (1U << id) 时须确保两处顺序一致
  */
typedef enum {
    CTRL_IN_IO1 = 0,                /* PB14 外部高电平检测1，低电平有效 */
    CTRL_IN_IO2,                    /* PC5  外部高电平检测2，低电平有效 */
    CTRL_IN_IO3,                    /* PC6  外部高电平检测3，低电平有效 */
    CTRL_IN_IO4,                    /* PC7  外部高电平检测4，低电平有效 */
    CTRL_IN_IO5,                    /* PC8  外部高电平检测5，低电平有效 */
    CTRL_IN_IO6,                    /* PC9  外部高电平检测6，低电平有效 */
    CTRL_IN_L1,                     /* PB12 外部低电平检测1，高电平有效 */
    CTRL_IN_L2,                     /* PB13 外部低电平检测2，高电平有效 */
    CTRL_IN_ELEC_SW,                /* PB2  电子锁状态检测，高电平开 */
    CTRL_IN_ACC,                    /* PA0  ACC输入检测，高电平有效 */
    CTRL_IN_MAX
} ctrl_in_id_e;

/**
  * @brief  控制模块输入状态
  * @note   位域表示数字量状态（0/1）；各位域顺序与 @ref ctrl_in_id_e 枚举值 0~9 严格对应，
  *         修改任一侧定义时须同步更新另一侧
  */
typedef struct {
    /* 高电平输入检测（6路，低电平有效，0=有效 1=无效） */
    uint16_t hdet_input1    : 1;   /**< PB14 高电平输入检测1 */
    uint16_t hdet_input2    : 1;   /**< PC5  高电平输入检测2 */
    uint16_t hdet_input3    : 1;   /**< PC6  高电平输入检测3 */
    uint16_t hdet_input4    : 1;   /**< PC7  高电平输入检测4 */
    uint16_t hdet_input5    : 1;   /**< PC8  高电平输入检测5 */
    uint16_t hdet_input6    : 1;   /**< PC9  高电平输入检测6 */

    /* 低电平输入检测（2路，高电平有效，1=有效 0=无效） */
    uint16_t ldet_input1     : 1;   /**< PB12 低电平输入检测1 */
    uint16_t ldet_input2     : 1;   /**< PB13 低电平输入检测2 */

    /* 电子锁状态（1路，高电平开） */
    uint16_t elec_sw        : 1;   /**< PB2  电子锁状态 */

    /* ACC输入（1路，高电平有效） */
    uint16_t acc_in         : 1;   /**< PA0  ACC输入检测 */

    /* 填充对齐（21位位域 + 11位填充 = 32位） */
    uint16_t reserved       : 6;
} ctrl_input_t;

/**
 * @brief  控制模块全局状态
 * @note   位域表示数字量状态（0/1），uint16_t表示模拟量原始值
 */
typedef struct {
    ctrl_input_t input;

    /* ADC检测原始值（3路） */
    uint16_t adc_ext_volt1;        /**< PC0 外部电压检测1（分压比732K/75K） */
    uint16_t adc_ext_volt2;        /**< PC1 外部电压检测2（分压比732K/75K） */
    uint16_t adc_pwr_volt;         /**< PC2 电源电压检测 */
} my_ctrl_state_t;

/*===========================================================================
 *  API接口
 *===========================================================================*/

/*********************************************************************
 * @brief   初始化并启动控制任务
 * @return  0: 成功  -1: 失败
 * @note    创建FreeRTOS任务和消息队列；重复调用安全返回0
 *********************************************************************/
int my_ctrl_init(void);

/*********************************************************************
 * @brief   获取当前输入状态（快照）
 * @return  ctrl_input_t  当前输入状态结构体（位域）
 * @note    直接读取s_state.input的快照，非线程安全但足够快；
 *          若需原子读取，应在CTRL任务内通过消息获取
 *********************************************************************/
ctrl_input_t my_ctrl_get_input_state(void);

/*********************************************************************
 * @brief   设置输出口状态
 * @param   id     输出口枚举ID (ctrl_out_id_e)
 * @param   state  true=开, false=关
 * @return  none
 * @note    供 LED/PWR/BUZZER 等子模块调用，统一输出控制入口
 *********************************************************************/
void my_ctrl_set_output(ctrl_out_id_e id, bool state);

#ifdef __cplusplus
}
#endif

#endif /* __MY_CTRL_H__ */