# 嵌入式C语言编程规范

---

## 1. 文件命名规范

- **格式**: 小写字母 + 下划线，禁止中文、空格、大写、特殊符号
- **驱动文件**: `drv_模块名.c/h`（示例：`drv_gpio.c`、`drv_uart.h`）
- **功能文件**: `my_功能名.c/h`（必须添加my前缀，示例：`my_uart_control.c`）
- **配置文件**: `config_xxx.c/h`（示例：`config_system.c`）

---

## 2. 文件头部注释

所有 .c 和 .h 文件顶部必须添加：

```c
/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       my_ctrl.c
**文件描述：       系统控制功能模块实现文件 (LED, Buzzer, Key)
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.01.15
*********************************************************************
** 功能描述：       1. 整合 LED 与蜂鸣器控制接口
**                 2. 实现独立线程处理按键扫描与逻辑
*********************************************************************/
```

---

## 3. 头文件规范

- **必须添加头文件保护**，格式：`__文件名大写_H__`
- 驱动文件使用drv前缀：`__DRV_GPIO_H__`
- 功能文件需包含my前缀：`__MY_UART_CONTROL_H__`
- **函数声明必须添加详细注释**（@brief、@param、@return、@note）

```c
#ifndef __DRV_GPIO_H__
#define __DRV_GPIO_H__

/*********************************************************************
 * @brief   GPIO驱动设置引脚高电平
 * @param   port GPIO端口基地址(DRV_GPIOA~DRV_GPIOE)
 * @param   pin 引脚掩码(DRV_GPIO_PIN_0~15)
 * @return  无
 * @note    直接封装gpio_bit_set()
 *********************************************************************/
void drv_gpio_set(drv_gpio_port_e port, drv_gpio_pin_e pin);

#endif
```

---

## 4. 函数注释规范

```c
/*********************************************************************
 * @brief   函数功能说明（简洁明了）
 * @param   参数名  参数说明
 * @return  返回值说明
 * @note    注意事项
 *********************************************************************/
```

**说明**: 无参数时省略@param，void返回值省略@return。

---

## 5. 缩进与格式

- **缩进**: 4个空格，禁止使用Tab
- **单行长度**: 不超过120字符
- **运算符**: 前后各保留1个空格（`a + b`）
- **逗号**: 后保留1个空格（`func(a, b, c)`）
- **大括号**: 独占一行

### 函数内部空行规范（重要）

1. 局部变量定义后，必须加1个空行
2. 循环/分支语句结束后，如果后面紧跟return，必须在return前加1个空行
3. 不同逻辑块之间必须加1个空行
4. 连续的同类型语句之间可不加空行

```c
int can_send_msg(uint8_t *data_buf, uint8_t len)
{
    static uint32_t sSendCnt = 0;

    if (data_buf == NULL || len == 0)
    {
        return -1;
    }

    uint8_t i;

    for (i = 0; i < len; i++)
    {
        _can_send_single_byte(data_buf[i]);
    }

    sSendCnt++;

    return 0;
}
```

---

## 6. 分支语句规范

- if/else关键字后必须加1个空格
- 条件表达式必须用括号括起来
- **无论代码块是否只有一行，必须加 `{ }`**
- else if、else 必须紧跟前一个代码块的右括号换行

```c
uint8_t valid_param = (data_buf != NULL) && (len <= 8);
if (valid_param)
{
    my_uart_control_send(data_buf, len, &uart_state);
}
else if (len > 8)
{
    return -2;
}
else
{
    return -1;
}
```

---

## 7. 循环语句规范

- for/while关键字后必须加1个空格
- **无论循环体是否只有一行，必须加 `{ }`**
- 循环变量尽量在循环内定义（C99）

```c
for (uint8_t i = 0; i < 8; i++)
{
    gUartRecvBuf[i] = CAN->RDHR >> (8 * i);
}

while (timeout < 1000)
{
    if (CAN->RF0R & 0x01)
    {
        break;
    }

    timeout++;
}
```

---

## 8. 函数命名规范

- **格式**: 小写字母 + 下划线
- **驱动函数**: `drv_模块名_动词_名词`（必须添加drv前缀，示例：`drv_gpio_init`、`drv_uart_send`）
- **功能函数**: `my_模块名_动词_名词`（必须添加my前缀，示例：`my_uart_control_send`）
- **静态函数**: `_drv_模块名_动词_名词`（驱动内部私有函数，示例：`_drv_gpio_enable_clock`）

### 统一动词规范

| 动词 | 说明 | 动词 | 说明 |
|------|------|------|------|
| init | 初始化 | start | 启动 |
| deinit | 去初始化 | stop | 停止 |
| open | 打开/使能 | set | 设置 |
| close | 关闭/失能 | get | 获取 |
| send | 发送 | read | 读取 |
| recv | 接收 | write | 写入 |
| enable | 使能 | check | 检查 |
| disable | 禁能 | clear | 清除 |

---

## 9. 变量命名规范

- **全局变量**: `g+驼峰命名法`（示例：`gUartRecvBuf`、`gDeviceState`）
- **静态变量**: `s+驼峰命名法`（示例：`sTimeoutCnt`、`sCrcVal`）
- **局部变量**: `小写字母+下划线`（示例：`led_state`、`recv_len`）
- **数组命名**: 后缀加 `_buf`（缓冲区）或 `_arr`（数组）

---

## 10. 宏与常量命名

- **格式**: 大写字母 + 下划线
- **数值宏必须加括号**: `#define MAX_LEN (8U)`
- **驱动类型宏**: `DRV_模块名_XXX`（示例：`DRV_GPIO_LOGE`、`DRV_MAX_GPIO_PORT_COUNT`）
- 功能模块宏添加my前缀：`MY_UART_CONTROL_MAX_LEN`

```c
/* 驱动日志宏（业界标准：LOGE/LOGW/LOGI/LOGD） */
#define DRV_GPIO_LOGE(fmt, ...)    MY_LOG_E("[GPIO] " fmt, ##__VA_ARGS__)
#define DRV_GPIO_LOGW(fmt, ...)    MY_LOG_W("[GPIO] " fmt, ##__VA_ARGS__)
#define DRV_GPIO_LOGI(fmt, ...)    MY_LOG_I("[GPIO] " fmt, ##__VA_ARGS__)
#define DRV_GPIO_LOGD(fmt, ...)    MY_LOG_D("[GPIO] " fmt, ##__VA_ARGS__)

/* 驱动配置宏 */
#define DRV_MAX_GPIO_PORT_COUNT      (5U)
#define DRV_MAX_GPIO_PIN_PER_PORT    (16U)
#define DRV_MAX_EXTI_LINE_COUNT      (16U)
```

---

## 11. 数据类型规范

- **统一使用 stdint.h 标准类型**，禁止使用 char、int、short、long
- **常用类型**:
  - `uint8_t`、`int8_t`
  - `uint16_t`、`int16_t`
  - `uint32_t`、`int32_t`
- **布尔类型**: 用 `uint8_t`，定义 `TRUE (1U)`、`FALSE (0U)`

---

## 12. 结构体与枚举命名

### 结构体
- **格式**: `小写字母+下划线+_t`
- **驱动结构体**: `drv_模块名_结构名_t`（示例：`drv_gpio_config_t`、`drv_uart_msg_t`）
- 功能模块添加my前缀：`my_uart_control_msg_t`

```c
typedef struct {
    drv_gpio_port_e port;         /**< GPIO端口基地址（DRV_GPIOA~DRV_GPIOE） */
    drv_gpio_pin_e pin;           /**< 引脚掩码（DRV_GPIO_PIN_0~15） */
    drv_gpio_mode_e mode;         /**< 工作模式（DRV_GPIO_MODE_OUTPUT/INPUT/AF/ANALOG） */
    drv_gpio_pupd_e pupd;         /**< 上下拉配置（DRV_GPIO_PUPD_NONE/PULLUP/PULLDOWN） */
    bool initial_state;           /**< 初始状态（true=高电平，false=低电平） */
} drv_gpio_config_t;
```

### 枚举
- **驱动枚举**: `drv_模块名_枚举名_e`（示例：`drv_gpio_port_e`、`drv_uart_state_e`）
- 枚举成员：`DRV_模块名_成员名`（示例：`DRV_GPIOA`、`DRV_UART_STATE_IDLE`）

```c
typedef enum {
    DRV_GPIOA = GPIOA,          /**< GPIOA端口 */
    DRV_GPIOB = GPIOB,          /**< GPIOB端口 */
    DRV_GPIOC = GPIOC,          /**< GPIOC端口 */
    DRV_GPIOD = GPIOD,          /**< GPIOD端口 */
    DRV_GPIOE = GPIOE           /**< GPIOE端口 */
} drv_gpio_port_e;

typedef enum {
    DRV_GPIO_MODE_OUTPUT = 0,   /**< 输出模式 */
    DRV_GPIO_MODE_INPUT,        /**< 输入模式 */
    DRV_GPIO_MODE_AF,           /**< 复用功能模式 */
    DRV_GPIO_MODE_ANALOG        /**< 模拟模式 */
} drv_gpio_mode_e;
```

---

## 13. 函数编写规范

- **单一职责**: 一个函数只实现一个核心功能，不超过100行
- **参数数量**: 不超过5个，超过时用结构体封装
- **无参数函数**: 必须显式写 `void`（`void led_init(void)`）
- **指针参数**: 使用前必须判空
- **局部变量**: 定义在函数开头，集中初始化

---

## 14. 代码安全规范

- **指针**: 使用前必须判空
- **数组**: 必须检查下标边界
- **除法**: 除数必须先判断不为0
- **中断**: 尽量简短，不执行耗时操作
- **共享资源**: 需加临界区保护
- **延时**: 优先使用定时器，禁止空循环延时

---

## 15. 模块化规范

- 一个文件实现一个模块/功能
- 模块间低耦合：仅通过头文件接口交互
- 模块内高内聚：相关功能集中在一个文件
- 对外接口：仅在头文件中声明，静态函数不对外暴露

---

## 16. 其他规范

- **编码**: 统一使用 UTF-8
- **调试代码**: 调试完成后必须删除
- **格式统一**: 整个项目严格遵循本规范

---

**文档版本**: V1.2
**最后更新**: 2026-04-20
**更新说明**:
- V1.2: 添加驱动层命名规范（drv_前缀体系）
- V1.2: 明确驱动/功能/静态函数命名规则
- V1.1: 完善函数内部空行规范
