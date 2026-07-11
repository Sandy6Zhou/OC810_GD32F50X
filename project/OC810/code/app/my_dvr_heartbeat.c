/********************************************************************
**版权所有：         深圳市几米物联有限公司
**文件名称：        my_dvr_heartbeat.c
**文件描述：        DVR视频模块双向心跳异常检测与自动重启策略实现
**当前版本：        V1.0
**作    者：        伍玉蛟 (wuyujiao@jimiiot.com)
**完成日期：        2026.06.16
*********************************************************************
** 功能描述：       1. 双向心跳监测（MCU→DVR / DVR→MCU）
**                 2. 通讯异常检测与自动重启策略
**                 3. 状态同步（MCU状态通过心跳传递给DVR）
*********************************************************************/

#include "my_comm.h"

/*********************************************************************
 * 内部宏定义
 *********************************************************************/

/** 心跳发送间隔（毫秒） */
#define D_HEARTBEAT_SEND_INTERVAL    (1000U)

/** 等待DVR心跳超时时间（毫秒），90秒未收到则判定异常 */
#define D_HEARTBEAT_WAIT_TIMEOUT     (90000U)

/** DVR报告值无变化判定阈值（连续90次） */
#define D_MCU_HB_STALE_THRESHOLD     (90U)

/** 最大重启尝试次数（超过则放弃） */
#define D_HEARTBEAT_MAX_RESTART_CNT  (3U)

/** 心跳包 payload 长度定义 */
#define D_MCU_HEARTBEAT_PAYLOAD_LEN  (12U)   /* timestamp(4) + status(8) */
#define D_DVR_HEARTBEAT_PAYLOAD_LEN  (10U)   /* heart_cnt(2) + status(8) */

/*********************************************************************
 * 内部辅助函数声明
 *********************************************************************/
static void my_dvr_heartbeat_request_restart(uint8_t reason);

/*********************************************************************
 * 内部全局变量
 *********************************************************************/

/** 连续重启尝试次数（>=3则放弃） */
static uint8_t s_restart_attempt_cnt = 0;

/** DVR报告的上次MCU心跳计数值 */
static uint16_t s_mcu_hb_last_cnt = 0;

/** DVR报告值连续无变化次数 */
static uint8_t s_mcu_hb_stale_cnt = 0;

/** 重启中标志（防止重启过程中心跳消息干扰计数器） */
static bool s_is_restarting = false;

/*********************************************************************
 * 内部辅助函数实现
 *********************************************************************/

/*********************************************************************
 * @brief   发送心跳定时器回调
 * @param   param  回调参数（未使用）
 * @return  none
 * @note    定时器上下文，发送消息通知主任务执行发送操作
 *********************************************************************/
static void my_dvr_send_heartbeat_cb(my_timer_handle_t timer_handle)
{
    (void)timer_handle;

    my_msg_t msg = {
        .id = MY_MSG_ID_DVR_SEND_HEARTBEAT,
        .data = NULL,
        .len = 0
    };

    my_msg_send(MSG_QUEUE_DVR, &msg, 0);
}

/*********************************************************************
 * @brief   等待心跳超时定时器回调
 * @param   param  回调参数（未使用）
 * @return  none
 * @note    定时器上下文，发送超时消息通知主任务
 *********************************************************************/
static void my_dvr_wait_heartbeat_cb(my_timer_handle_t timer_handle)
{
    (void)timer_handle;

    my_msg_t msg = {
        .id = MY_MSG_ID_DVR_WAIT_HEARTBEAT_TOUT,
        .data = NULL,
        .len = 0
    };

    my_msg_send(MSG_QUEUE_DVR, &msg, 0);
}

/*********************************************************************
 * @brief   构造并发送MCU心跳帧（MCU→DVR）
 * @return  none
 * @note    命令码：D_MCU_CMD_HEARTBEAT = 0x2002
 *
 *          Payload（12字节）
 *          偏移  类型        字段          说明
 *          0     DWORD(BE)   timestamp     UTC时间戳
 *          4     BYTE[8]     status        MCU设备状态（位域定义见MCU侧）
 *
 *          当前timestamp和status均填0，后续对接系统状态时填充
 *********************************************************************/
static void my_dvr_heartbeat_send(void)
{
    uint8_t payload[D_MCU_HEARTBEAT_PAYLOAD_LEN] = {0};

    /* TODO: 填充timestamp和status */
    my_dvr_cmd_send_request(D_MCU_CMD_HEARTBEAT, payload, sizeof(payload));
}

/*********************************************************************
 * @brief   处理DVR心跳超时（90秒未收到）
 * @return  none
 * @note    情况1：MCU收不到DVR心跳，触发重启策略
 *          防重入：如果已在重启中，忽略重复超时消息
 *********************************************************************/
static void my_dvr_heartbeat_on_timeout(void)
{
    /* 防止超时消息重复触发重启 */
    if (s_restart_attempt_cnt > 0 &&
        s_restart_attempt_cnt <= D_HEARTBEAT_MAX_RESTART_CNT)
    {
        MY_LOG_D("Restart in progress, ignore timeout");
        return;
    }

    MY_LOG_W("DVR heartbeat lost (90s timeout)");
    my_dvr_heartbeat_request_restart(1);  /* reason=1: MCU收不到DVR心跳 */
}

/*********************************************************************
 * @brief   请求重启DVR模块
 * @param   reason  重启原因（1=MCU收不到DVR心跳, 2=DVR收不到MCU心跳）
 * @return  none
 * @note    连续重启3次失败则放弃，记录设备故障
 *          设置重启中标志，防止心跳消息干扰计数器
 *********************************************************************/
static void my_dvr_heartbeat_request_restart(uint8_t reason)
{
    my_msg_t msg = {
        .id = MY_MSG_ID_DVR_HEARTBEAT_RESTART,
        .data = NULL,
        .len = reason
    };

    /* 停止心跳定时器，等待重启后重新建立 */
    my_timer_stop(MY_TIMER_ID_DVR_SEND_HEARTBEAT);
    my_timer_stop(MY_TIMER_ID_DVR_WAIT_HEARTBEAT);

    s_restart_attempt_cnt++;
    s_is_restarting = true;  /* 标记重启中 */

    if (s_restart_attempt_cnt > D_HEARTBEAT_MAX_RESTART_CNT)
    {
        MY_LOG_E("DVR restart failed %u times, device fault (reason=%u)",
                 s_restart_attempt_cnt - 1, reason);
        s_is_restarting = false;  /* 放弃重启，清除标志 */
        return;  /* 放弃重启，设备故障 */
    }

    MY_LOG_W("DVR heartbeat restart %u/%u (reason=%u)",
             s_restart_attempt_cnt, D_HEARTBEAT_MAX_RESTART_CNT, reason);

    my_msg_send(MSG_QUEUE_DVR, &msg, 0);
}

/*********************************************************************
 *  公开API实现
 *********************************************************************/

/*********************************************************************
 * @brief   心跳消息处理入口
 * @param   msg  消息指针
 * @return  none
 * @note    HEARTBEAT_RCV 的 msg->len 携带 DVR报告的 mcu_hb_cnt
 *********************************************************************/
void my_dvr_heartbeat_on_msg(const my_msg_t *msg)
{
    switch (msg->id)
    {
        case MY_MSG_ID_DVR_SEND_HEARTBEAT:
            my_dvr_heartbeat_send();
            break;

        case MY_MSG_ID_DVR_WAIT_HEARTBEAT_TOUT:
            my_dvr_heartbeat_on_timeout();
            break;

        default:
            break;
    }
}


/*********************************************************************
 * @brief   处理收到DVR心跳包
 * @param   mcu_hb_cnt  DVR报告的收到MCU心跳计数（主机字节序）
 * @return  none
 * @note    情况1：重置超时计时器
 *          情况2：检查 mcu_hb_cnt 是否变化，连续90次不变则判定异常
 *          保护：重启中忽略心跳，防止计数器被意外清零
 *********************************************************************/
void my_dvr_heartbeat_on_rcv(uint16_t mcu_hb_cnt)
{
    /* 重启中忽略心跳，防止计数器被意外清零 */
    if (s_is_restarting)
    {
        return;
    }

    /* 情况1：收到DVR心跳，重置超时计时器 */
    s_restart_attempt_cnt = 0;
    my_timer_start(MY_TIMER_ID_DVR_WAIT_HEARTBEAT, 0);

    /* 情况2：检查DVR是否能收到MCU心跳 */
    if (mcu_hb_cnt == s_mcu_hb_last_cnt)
    {
        s_mcu_hb_stale_cnt++;
        if (s_mcu_hb_stale_cnt >= D_MCU_HB_STALE_THRESHOLD)
        {
            MY_LOG_W("DVR cannot receive MCU heartbeat (%u times)",
                     s_mcu_hb_stale_cnt);
            my_dvr_heartbeat_request_restart(2);  /* reason=2: DVR收不到MCU心跳 */
        }
    }
    else
    {
        s_mcu_hb_last_cnt = mcu_hb_cnt;
        s_mcu_hb_stale_cnt = 0;
    }
}
/*********************************************************************
 * @brief   启动心跳接收监测
 * @return  none
 * @note    DVR电源开启后调用，启动90秒超时定时器
 *          此时仅监测DVR→MCU方向，不发送心跳
 *          清除重启中标志（重启完成）
 *********************************************************************/
void my_dvr_heartbeat_start(void)
{
    s_restart_attempt_cnt = 0;
    s_mcu_hb_last_cnt = 0;
    s_mcu_hb_stale_cnt = 0;
    s_is_restarting = false;  /* 重启完成，清除标志 */
    my_timer_start(MY_TIMER_ID_DVR_WAIT_HEARTBEAT, 0);
}

/*********************************************************************
 * @brief   确认DVR通讯建立，启动心跳发送
 * @return  none
 * @note    收到DVR心跳或版本查询时调用：
 *          1. 启动 MCU→DVR 心跳发送定时器
 *          2. 立即发送一个心跳包
 *********************************************************************/
void my_dvr_heartbeat_on_contact(void)
{
    if (!my_timer_is_running(MY_TIMER_ID_DVR_SEND_HEARTBEAT))
    {
        MY_LOG_I("DVR contact confirmed, start heartbeat send");
        my_timer_start(MY_TIMER_ID_DVR_SEND_HEARTBEAT, 0);
        my_dvr_heartbeat_send();   /* 立即发送一个心跳 */
    }
}

/*********************************************************************
 * @brief   停止心跳模块
 * @return  none
 * @note    电源关闭前调用，停止发送心跳和超时监测
 *          清除重启中标志（状态重置）
 *********************************************************************/
void my_dvr_heartbeat_stop(void)
{
    my_timer_stop(MY_TIMER_ID_DVR_SEND_HEARTBEAT);
    my_timer_stop(MY_TIMER_ID_DVR_WAIT_HEARTBEAT);
    s_restart_attempt_cnt = 0;
    s_mcu_hb_last_cnt = 0;
    s_mcu_hb_stale_cnt = 0;
    s_is_restarting = false;  /* 状态重置 */
}

/*********************************************************************
 * @brief   心跳模块初始化
 * @return  none
 *********************************************************************/
void my_dvr_heartbeat_init(void)
{
    if (my_timer_create(MY_TIMER_ID_DVR_SEND_HEARTBEAT,
                        my_dvr_send_heartbeat_cb,
                        D_HEARTBEAT_SEND_INTERVAL))
    {
        MY_LOG_E("heartbeat send timer create failed");
    }

    if (my_timer_create(MY_TIMER_ID_DVR_WAIT_HEARTBEAT,
                        my_dvr_wait_heartbeat_cb,
                        D_HEARTBEAT_WAIT_TIMEOUT))
    {
        MY_LOG_E("heartbeat wait timer create failed");
    }

    s_restart_attempt_cnt = 0;
    s_mcu_hb_last_cnt = 0;
    s_mcu_hb_stale_cnt = 0;
    s_is_restarting = false;  /* 初始化状态 */
}
