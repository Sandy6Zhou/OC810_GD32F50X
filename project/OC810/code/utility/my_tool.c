/********************************************************************
**版权所有：         深圳市几米物联有限公司
**文件名称：        my_tool.c
**文件描述：        通用工具模块实现
**当前版本：        V2.0
**作    者：        伍玉蛟 (wuyujiao@jimiiot.com)
**完成日期：        2026.06.08
*********************************************************************
** 功能描述：       1. 字符串/十六进制转换工具
**                 2. 数字/时间/坐标校验与解析
**                 3. CRC16-CCITT 校验
**                 4. MD5 流式计算（RFC 1321标准）
**                 5. 地理围栏计算
** 说明：           由 Zephyr 项目迁移并适配 GD32+FreeRTOS 平台
*********************************************************************/
#include "my_tool.h"

/*********************************************************************
 * 内部宏定义
 *********************************************************************/

/** 数学圆周率 */
#define D_MATH_PI               (3.14159265358979)

/** 地球半径（米，WGS84 平均半径） */
#define D_EARTH_RADIUS_M        (6371000.0)

/** 微度转弧度的换算系数：PI / 180 / 1000000 */
#define D_MICRO_DEG_TO_RAD      (D_MATH_PI / 180000000.0)

/*********************************************************************
 * 内部辅助函数声明
 *********************************************************************/
static uint8_t hex_char_to_value(char c);
static uint32_t calculate_distance_m(int32_t lat1, int32_t lon1,
                                      int32_t lat2, int32_t lon2);

/*********************************************************************
 *  字符串/十六进制转换工具实现
 *********************************************************************/

/*********************************************************************
 * @brief   按分隔符分割字符串，提取指定位置的字段
 * @param   sz_input   输入字符串（会被遍历，不会被修改）
 * @param   i_pos      字段位置（从 0 开始）
 * @param   c_split    分隔符
 * @param   sz_out_buf 输出缓冲区
 * @param   i_buff_len 输出缓冲区容量（需预留 '\0' 空间）
 * @return  true: 还有后续字段  false: 已到末尾
 * @note    防溢出：输出长度不超过 i_buff_len-1
 *********************************************************************/
bool my_tool_str_split(char *sz_input, uint16_t i_pos, char c_split,
                       char *sz_out_buf, uint16_t i_buff_len)
{
    uint16_t i = 0;
    char *c;
    char *p;
    bool b_has_more = false;

    if (sz_input == NULL || sz_out_buf == NULL || i_buff_len == 0)
    {
        return false;
    }

    c = sz_input;
    p = sz_out_buf;

    while (*c != '\0')
    {
        if (i == i_pos && *c != c_split)
        {
            *p++ = *c;

            /* 防止溢出，预留结尾符空间 */
            if (p >= (sz_out_buf + i_buff_len - 1))
            {
                break;
            }
        }
        else if (i > i_pos)
        {
            b_has_more = true;
            break;
        }

        if (*c == c_split)
        {
            i++;
        }

        c++;
    }

    *p = '\0';
    return b_has_more;
}

/*********************************************************************
 * @brief   检测字符串是否全为十六进制字符
 * @param   str 输入字符串
 * @return  字符串长度；0 表示错误（含非十六进制字符或空指针）
 *********************************************************************/
uint8_t my_tool_chk_hexstr(const char *str)
{
    uint8_t count = 0;

    if (str == NULL)
    {
        return 0;
    }

    while (*str != '\0')
    {
        if ((*str >= '0' && *str <= '9') ||
            (*str >= 'a' && *str <= 'f') ||
            (*str >= 'A' && *str <= 'F'))
        {
            count++;
            str++;
        }
        else
        {
            return 0;
        }
    }

    return count;
}

/*********************************************************************
 * @brief   十六进制字符串转换为字节数组
 * @param   dest      目标数组
 * @param   dest_size 目标数组容量
 * @param   src       十六进制字符串（如 "0A1B"）
 * @return  实际写入字节数；0 表示错误
 * @note    偶数位示例："0A1B" → {0x0A, 0x1B}
 *          奇数位时末半字节低位补零："ABC" → {0xAB, 0xC0}
 *********************************************************************/
uint8_t my_tool_hexstr2bin(uint8_t *dest, uint8_t dest_size, const char *src)
{
    uint8_t offset = 0;
    uint8_t temp_data = 0;
    uint8_t hex_data = 0;
    uint8_t byte_count = 0;

    if (dest == NULL || src == NULL || dest_size == 0)
    {
        return 0;
    }

    while (*src != '\0')
    {
        if (*src >= '0' && *src <= '9')
        {
            temp_data = (uint8_t)(*src - '0');
        }
        else if (*src >= 'A' && *src <= 'F')
        {
            temp_data = (uint8_t)(*src - 'A' + 0x0A);
        }
        else if (*src >= 'a' && *src <= 'f')
        {
            temp_data = (uint8_t)(*src - 'a' + 0x0A);
        }
        else
        {
            return 0;
        }

        if (offset % 2)
        {
            hex_data |= (temp_data & 0x0F);
            dest[byte_count++] = hex_data;

            if (byte_count >= dest_size)
            {
                break;
            }
        }
        else
        {
            hex_data = (uint8_t)((temp_data << 4) & 0xF0);
        }

        offset++;
        src++;
    }

    /* 奇数位：末半字节低位补零后写入 */
    if ((offset % 2) == 1 && byte_count < dest_size)
    {
        dest[byte_count++] = hex_data;
    }

    return byte_count;
}

/*********************************************************************
 * @brief   十六进制数字转换为 ASCII 字符
 * @param   digit 0~15 的十六进制数字
 * @return  ASCII 字符（'0'~'9', 'A'~'F'）
 *********************************************************************/
uint8_t my_tool_hex2ascii(uint8_t digit)
{
    if (digit <= 9)
    {
        return (uint8_t)(digit + '0');
    }
    else
    {
        return (uint8_t)(digit - 0x0A + 'A');
    }
}

/*********************************************************************
 * @brief   字节数组转换为十六进制字符串
 * @param   hex     输入字节数组
 * @param   hex_len 字节数组长度
 * @param   str     输出字符串缓冲区
 * @param   str_len 输出缓冲区容量（需 >= 2 * hex_len + 1）
 * @note    输出大写十六进制字符，末尾自动添加 '\0'
 *********************************************************************/
void my_tool_bin2hexstr(const uint8_t *hex, uint16_t hex_len,
                        uint8_t *str, uint16_t str_len)
{
    uint16_t i;
    uint16_t j = 0;

    if (hex == NULL || str == NULL)
    {
        return;
    }

    if (str_len < (uint16_t)(2U * hex_len + 1U))
    {
        return;
    }

    for (i = 0; i < hex_len; i++)
    {
        str[j++] = my_tool_hex2ascii((uint8_t)((hex[i] >> 4) & 0x0F));
        str[j++] = my_tool_hex2ascii((uint8_t)(hex[i] & 0x0F));
    }

    str[j] = '\0';
}

/*********************************************************************
 * @brief   十六进制字符转换为数值（内部辅助）
 * @param   c 十六进制字符（'0'~'9', 'A'~'F', 'a'~'f'）
 * @return  0~15 的数值；非法字符返回 0xFF
 *********************************************************************/
static uint8_t hex_char_to_value(char c)
{
    if (c >= '0' && c <= '9')
    {
        return (uint8_t)(c - '0');
    }
    else if (c >= 'A' && c <= 'F')
    {
        return (uint8_t)(c - 'A' + 0x0A);
    }
    else if (c >= 'a' && c <= 'f')
    {
        return (uint8_t)(c - 'a' + 0x0A);
    }
    else
    {
        return 0xFFU;
    }
}

/*********************************************************************
 * @brief   MAC 地址字符串转换为 6 字节数组
 * @param   mac_str MAC 字符串（支持 "AA:BB:CC:DD:EE:FF" 或 "AABBCCDDEEFF"）
 * @param   hex     输出 6 字节数组
 * @return  true: 成功  false: 格式错误或参数为空
 * @note    仅接受 12 位紧凑格式或 17 位带 ':' 分隔格式
 *********************************************************************/
bool my_tool_mac2bin(const char *mac_str, uint8_t *hex)
{
    uint16_t len;
    uint16_t offset = 0;
    uint8_t hex_data_count = 0;
    uint8_t hex_h;
    uint8_t hex_l;

    if (mac_str == NULL || hex == NULL)
    {
        return false;
    }

    len = (uint16_t)strlen(mac_str);

    /* 只接受两种格式：12 位紧凑 或 17 位带 ':' 分隔 */
    if (len != 12 && len != 17)
    {
        return false;
    }

    while (offset < len)
    {
        if (mac_str[offset] == ':')
        {
            /* ':' 只能出现在特定位置 */
            if (offset == 2 || offset == 5 || offset == 8 ||
                offset == 11 || offset == 14)
            {
                offset++;
            }
            else
            {
                return false;
            }
        }
        else
        {
            hex_h = hex_char_to_value(mac_str[offset++]);
            hex_l = hex_char_to_value(mac_str[offset++]);

            if (hex_h == 0xFFU || hex_l == 0xFFU)
            {
                return false;
            }

            hex_data_count++;
            if (hex_data_count > 6)
            {
                return false;
            }

            *hex++ = (uint8_t)(((hex_h << 4) & 0xF0) | (hex_l & 0x0F));
        }
    }

    return true;
}

/*********************************************************************
 * @brief   字节数组整体逆序
 * @param   src      源数组
 * @param   src_len  源数组长度
 * @param   dest     目标数组
 * @param   dest_len 目标数组容量
 * @return  0: 成功  -1: 参数无效  -2: src_len 与 dest_len 不一致
 * @note    纯字节操作，不添加字符串终止符
 *********************************************************************/
int my_tool_memrev(const uint8_t *src, uint32_t src_len,
                   uint8_t *dest, uint32_t dest_len)
{
    uint32_t i;

    if (src == NULL || dest == NULL || src_len == 0 || dest_len == 0)
    {
        return -1;
    }

    if (src_len != dest_len)
    {
        return -2;
    }

    for (i = 0; i < src_len; i++)
    {
        dest[i] = src[src_len - 1U - i];
    }

    return 0;
}

/*********************************************************************
 *  数字/整数校验工具实现
 *********************************************************************/

/*********************************************************************
 * @brief   检测字符串是否由数字及符号组成
 * @param   flag 标志位：bit0 允许 '+' 或 '-' 前缀
 *                       bit1 允许 '.' 小数点（仅一次）
 *                       其余仅允许纯数字 '0'~'9'
 * @param   str  输入字符串
 * @return  有效字符数；0 表示格式错误
 *********************************************************************/
uint8_t my_tool_chk_numstr(uint8_t flag, const char *str)
{
    uint8_t count = 0;

    if (str == NULL)
    {
        return 0;
    }

    while (*str != '\0')
    {
        if ((flag & 1U) && count == 0 && (*str == '+' || *str == '-'))
        {
            count++;
            str++;
        }
        else if ((flag & 2U) && *str == '.')
        {
            flag &= (uint8_t)~2U;   /* 小数点只允许出现一次 */
            count++;
            str++;
        }
        else if (*str >= '0' && *str <= '9')
        {
            count++;
            str++;
        }
        else
        {
            return 0;
        }
    }

    return count;
}

/*********************************************************************
 * @brief   判断字节数组是否表示有效整数
 * @param   arr     字节数组（ASCII 编码）
 * @param   arr_len 数组长度
 * @return  true: 有效整数格式  false: 无效
 * @note    整数格式：可选符号（'+' 或 '-'）后跟至少一个数字
 *********************************************************************/
bool my_tool_is_integer(const uint8_t *arr, uint32_t arr_len)
{
    uint32_t i;
    bool has_digit = false;

    if (arr == NULL || arr_len == 0)
    {
        return false;
    }

    /* 检查第一个字符：允许符号 '+' 或 '-' */
    i = 0;
    if (arr[0] == '+' || arr[0] == '-')
    {
        i = 1U;

        /* 仅有符号无数字 → 无效 */
        if (arr_len == 1U)
        {
            return false;
        }
    }

    /* 剩余字符必须全为数字 */
    for (; i < arr_len; i++)
    {
        if (!isdigit((unsigned char)arr[i]))
        {
            return false;
        }
        has_digit = true;
    }

    return has_digit;
}

/*********************************************************************
 *  时间/坐标解析工具实现
 *********************************************************************/

/*********************************************************************
 * @brief   解析经纬度字符串为微度值
 * @param   coord_str   经纬度字符串
 * @param   is_latitude 1: 纬度  0: 经度
 * @param   value       输出：解析后的微度值（南/西为负）
 * @param   valid       输出：1 有效  0 无效（空字符串时）
 * @return  0: 成功  -1: 格式错误或参数为空
 * @note    支持格式：
 *          1. 方向前缀：N/S（纬度）、E/W（经度）
 *          2. 正负号前缀：+ 北/东  - 南/西
 *          禁止混合符号（如 N-22277120、+S22277120 为非法）
 *********************************************************************/
int my_tool_parse_coord(const char *coord_str, int is_latitude,
                        int32_t *value, uint8_t *valid)
{
    int32_t abs_value;
    int sign;
    int i;
    int len;
    int digit_count;
    char first_char;

    if (coord_str == NULL || value == NULL || valid == NULL)
    {
        return -1;
    }

    len = (int)strlen(coord_str);

    /* 空字符串标记无效 */
    if (len == 0)
    {
        *value = 0;
        *valid = 0;
        return 0;
    }

    first_char = coord_str[0];
    sign = 1;

    /* 处理方向符号前缀（N/S/E/W） */
    if (first_char == 'N' || first_char == 'S' ||
        first_char == 'E' || first_char == 'W')
    {
        if (is_latitude)
        {
            if (first_char != 'N' && first_char != 'S')
            {
                return -1;
            }
            if (first_char == 'S')
            {
                sign = -1;
            }
        }
        else
        {
            if (first_char != 'E' && first_char != 'W')
            {
                return -1;
            }
            if (first_char == 'W')
            {
                sign = -1;
            }
        }
    }
    /* 处理正负号前缀 */
    else if (first_char == '+')
    {
        sign = 1;
    }
    else if (first_char == '-')
    {
        sign = -1;
    }
    else if (first_char < '0' || first_char > '9')
    {
        return -1;  /* 非法首字符 */
    }

    abs_value = 0;
    digit_count = 0;

    /* 从符号后的第一个数字开始解析 */
    i = (first_char == 'N' || first_char == 'S' ||
         first_char == 'E' || first_char == 'W' ||
         first_char == '+' || first_char == '-') ? 1 : 0;

    for (; i < len; i++)
    {
        if (coord_str[i] >= '0' && coord_str[i] <= '9')
        {
            abs_value = abs_value * 10 + (coord_str[i] - '0');
            digit_count++;
        }
        else
        {
            return -1;  /* 非法字符 */
        }
    }

    if (digit_count == 0)
    {
        return -1;
    }

    /* 范围校验 */
    if (is_latitude)
    {
        if (abs_value > 90000000)   /* 纬度：±90° */
        {
            return -1;
        }
    }
    else
    {
        if (abs_value > 180000000)  /* 经度：±180° */
        {
            return -1;
        }
    }

    *value = abs_value * sign;
    *valid = 1;
    return 0;
}

/*********************************************************************
 * @brief   校验时间字符串格式（YYMMDDHHMM）
 * @param   time_str 时间字符串
 * @param   valid    输出：1 有效  0 无效（空字符串时）
 * @return  0: 成功  -1: 格式错误或参数为空
 * @note    空字符串表示不限制，返回成功且 valid=0
 *          非空必须为 10 位数字，范围：
 *          YY: 00-99  MM: 01-12  DD: 按月份+闰年动态
 *          HH: 00-23  MM: 00-59
 *********************************************************************/
int my_tool_chk_time(const char *time_str, uint8_t *valid)
{
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int i;
    int time_len;
    int max_day;
    int is_leap_year;

    if (time_str == NULL || valid == NULL)
    {
        return -1;
    }

    time_len = (int)strlen(time_str);

    /* 空字符串表示不限制 */
    if (time_len == 0)
    {
        *valid = 0;
        return 0;
    }

    if (time_len != 10)
    {
        return -1;
    }

    /* 必须全为数字 */
    for (i = 0; i < 10; i++)
    {
        if (time_str[i] < '0' || time_str[i] > '9')
        {
            return -1;
        }
    }

    /* 解析各字段 */
    year   = (time_str[0] - '0') * 10 + (time_str[1] - '0');
    month  = (time_str[2] - '0') * 10 + (time_str[3] - '0');
    day    = (time_str[4] - '0') * 10 + (time_str[5] - '0');
    hour   = (time_str[6] - '0') * 10 + (time_str[7] - '0');
    minute = (time_str[8] - '0') * 10 + (time_str[9] - '0');

    if (month < 1 || month > 12)    return -1;
    if (hour < 0  || hour > 23)     return -1;
    if (minute < 0 || minute > 59)  return -1;

    /* 闰年判定 */
    is_leap_year = ((year % 4 == 0 && year % 100 != 0) ||
                    (year % 400 == 0)) ? 1 : 0;

    /* 按月份确定最大天数 */
    max_day = 31;
    if (month == 4 || month == 6 || month == 9 || month == 11)
    {
        max_day = 30;
    }
    else if (month == 2)
    {
        max_day = is_leap_year ? 29 : 28;
    }

    if (day < 1 || day > max_day)
    {
        return -1;
    }

    *valid = 1;
    return 0;
}

/*********************************************************************
 * @brief   时间字符串转 Unix 时间戳
 * @param   time_str 时间字符串，格式 "YYMMDDHHMM"
 * @return  时间戳（秒）；失败返回 (time_t)-1
 * @note    年份处理：YY + 2000，使用 mktime 自动处理时区
 *********************************************************************/
time_t my_tool_time2ts(const char *time_str)
{
    struct tm tm_time;
    int year;
    int month;
    int day;
    int hour;
    int min;
    int ret;

    if (time_str == NULL)
    {
        return (time_t)-1;
    }

    ret = sscanf(time_str, "%2d%2d%2d%2d%2d", &year, &month, &day, &hour, &min);
    if (ret != 5)
    {
        return (time_t)-1;
    }

    (void)memset(&tm_time, 0, sizeof(tm_time));
    tm_time.tm_year = year + 100;   /* struct tm: 从 1900 年起算 */
    tm_time.tm_mon  = month - 1;    /* struct tm: 0-11 */
    tm_time.tm_mday = day;
    tm_time.tm_hour = hour;
    tm_time.tm_min  = min;
    tm_time.tm_sec  = 0;
    tm_time.tm_isdst = -1;

    return mktime(&tm_time);
}

/*********************************************************************
 * @brief   判断当前时间是否在指定范围内
 * @param   start_time  开始时间，格式 "YYMMDDHHMM"
 * @param   end_time    结束时间，格式 "YYMMDDHHMM"
 * @param   current_ts  当前时间戳（秒）
 * @return  1: 在范围内（含边界）  0: 不在范围内  -1: 参数错误
 *********************************************************************/
int my_tool_in_timerange(const char *start_time, const char *end_time,
                         time_t current_ts)
{
    time_t start_ts;
    time_t end_ts;

    if (start_time == NULL || end_time == NULL)
    {
        return -1;
    }

    start_ts = my_tool_time2ts(start_time);
    end_ts   = my_tool_time2ts(end_time);

    if (start_ts == (time_t)-1 || end_ts == (time_t)-1)
    {
        return -1;
    }

    if (current_ts >= start_ts && current_ts <= end_ts)
    {
        return 1;
    }

    return 0;
}

/*********************************************************************
 *  CRC校验工具实现
 *********************************************************************/

/* CRC16-CCITT 查找表（256 项，多项式 0x1021） */
static const uint16_t crc16_table[256] =
{
    0X0000, 0X1189, 0X2312, 0X329B, 0X4624, 0X57AD, 0X6536, 0X74BF,
    0X8C48, 0X9DC1, 0XAF5A, 0XBED3, 0XCA6C, 0XDBE5, 0XE97E, 0XF8F7,
    0X1081, 0X0108, 0X3393, 0X221A, 0X56A5, 0X472C, 0X75B7, 0X643E,
    0X9CC9, 0X8D40, 0XBFDB, 0XAE52, 0XDAED, 0XCB64, 0XF9FF, 0XE876,
    0X2102, 0X308B, 0X0210, 0X1399, 0X6726, 0X76AF, 0X4434, 0X55BD,
    0XAD4A, 0XBCC3, 0X8E58, 0X9FD1, 0XEB6E, 0XFAE7, 0XC87C, 0XD9F5,
    0X3183, 0X200A, 0X1291, 0X0318, 0X77A7, 0X662E, 0X54B5, 0X453C,
    0XBDCB, 0XAC42, 0X9ED9, 0X8F50, 0XFBEF, 0XEA66, 0XD8FD, 0XC974,
    0X4204, 0X538D, 0X6116, 0X709F, 0X0420, 0X15A9, 0X2732, 0X36BB,
    0XCE4C, 0XDFC5, 0XED5E, 0XFCD7, 0X8868, 0X99E1, 0XAB7A, 0XBAF3,
    0X5285, 0X430C, 0X7197, 0X601E, 0X14A1, 0X0528, 0X37B3, 0X263A,
    0XDECD, 0XCF44, 0XFDDF, 0XEC56, 0X98E9, 0X8960, 0XBBFB, 0XAA72,
    0X6306, 0X728F, 0X4014, 0X519D, 0X2522, 0X34AB, 0X0630, 0X17B9,
    0XEF4E, 0XFEC7, 0XCC5C, 0XDDD5, 0XA96A, 0XB8E3, 0X8A78, 0X9BF1,
    0X7387, 0X620E, 0X5095, 0X411C, 0X35A3, 0X242A, 0X16B1, 0X0738,
    0XFFCF, 0XEE46, 0XDCDD, 0XCD54, 0XB9EB, 0XA862, 0X9AF9, 0X8B70,
    0X8408, 0X9581, 0XA71A, 0XB693, 0XC22C, 0XD3A5, 0XE13E, 0XF0B7,
    0X0840, 0X19C9, 0X2B52, 0X3ADB, 0X4E64, 0X5FED, 0X6D76, 0X7CFF,
    0X9489, 0X8500, 0XB79B, 0XA612, 0XD2AD, 0XC324, 0XF1BF, 0XE036,
    0X18C1, 0X0948, 0X3BD3, 0X2A5A, 0X5EE5, 0X4F6C, 0X7DF7, 0X6C7E,
    0XA50A, 0XB483, 0X8618, 0X9791, 0XE32E, 0XF2A7, 0XC03C, 0XD1B5,
    0X2942, 0X38CB, 0X0A50, 0X1BD9, 0X6F66, 0X7EEF, 0X4C74, 0X5DFD,
    0XB58B, 0XA402, 0X9699, 0X8710, 0XF3AF, 0XE226, 0XD0BD, 0XC134,
    0X39C3, 0X284A, 0X1AD1, 0X0B58, 0X7FE7, 0X6E6E, 0X5CF5, 0X4D7C,
    0XC60C, 0XD785, 0XE51E, 0XF497, 0X8028, 0X91A1, 0XA33A, 0XB2B3,
    0X4A44, 0X5BCD, 0X6956, 0X78DF, 0X0C60, 0X1DE9, 0X2F72, 0X3EFB,
    0XD68D, 0XC704, 0XF59F, 0XE416, 0X90A9, 0X8120, 0XB3BB, 0XA232,
    0X5AC5, 0X4B4C, 0X79D7, 0X685E, 0X1CE1, 0X0D68, 0X3FF3, 0X2E7A,
    0XE70E, 0XF687, 0XC41C, 0XD595, 0XA12A, 0XB0A3, 0X8238, 0X93B1,
    0X6B46, 0X7ACF, 0X4854, 0X59DD, 0X2D62, 0X3CEB, 0X0E70, 0X1FF9,
    0XF78F, 0XE606, 0XD49D, 0XC514, 0XB1AB, 0XA022, 0X92B9, 0X8330,
    0X7BC7, 0X6A4E, 0X58D5, 0X495C, 0X3DE3, 0X2C6A, 0X1EF1, 0X0F78
};

/*********************************************************************
 * @brief   CRC16-CCITT 计算（位运算法）
 * @param   data       数据缓冲区
 * @param   len        数据长度
 * @param   polynomial 多项式（常用：0x1021 CCITT / 0xA001 MODBUS）
 * @return  CRC16 校验值；data 为空时返回 0
 * @note    初始值 0xFFFF，异或输出 0x0000
 *********************************************************************/
uint16_t my_tool_crc16_bitwise(const uint8_t *data, uint16_t len, uint16_t polynomial)
{
    uint16_t crc = 0xFFFFU;     /* 初始值 0xFFFF（CCITT 标准） */
    uint16_t i;
    uint8_t j;

    if (data == NULL)
    {
        return 0;
    }

    for (i = 0; i < len; i++)
    {
        crc ^= (uint16_t)data[i] << 8;

        for (j = 0; j < 8; j++)
        {
            if (crc & 0x8000U)
            {
                crc = (uint16_t)((crc << 1) ^ polynomial);
            }
            else
            {
                crc = (uint16_t)(crc << 1);
            }
        }
    }

    return crc;
}

/*********************************************************************
 * @brief   CRC16-CCITT 计算（查表法）
 * @param   data  数据缓冲区
 * @param   len   数据长度
 * @return  CRC16 校验值；data 为空时返回 0
 * @note    固定 CCITT 多项式 0x1021，初始值 0xFFFF，输出取反
 *          使用 256 项预计算表，每字节只需一次查表+异或操作
 *********************************************************************/
uint16_t my_tool_crc16_table(const uint8_t *data, uint16_t len)
{
    uint16_t fcs = 0xFFFFU;  /* 初始值 0xFFFF */
    uint16_t i;

    if (data == NULL)
    {
        return 0;
    }

    for (i = 0; i < len; i++)
    {
        fcs = (uint16_t)((fcs >> 8) ^ crc16_table[(fcs ^ data[i]) & 0xFF]);
    }

    return (uint16_t)(~fcs);  /* 输出取反 */
}

/*********************************************************************
 *  MD5流式计算实现（RFC 1321标准）
 *********************************************************************/

/* MD5 基础操作宏 */
#define MD5_F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define MD5_G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define MD5_H(x, y, z) ((x) ^ (y) ^ (z))
#define MD5_I(x, y, z) ((y) ^ ((x) | (~z)))

#define MD5_ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

#define MD5_FF(a, b, c, d, x, s, ac) \
    do { \
        (a) += MD5_F((b), (c), (d)) + (x) + (uint32_t)(ac); \
        (a) = MD5_ROTATE_LEFT((a), (s)); \
        (a) += (b); \
    } while (0)

#define MD5_GG(a, b, c, d, x, s, ac) \
    do { \
        (a) += MD5_G((b), (c), (d)) + (x) + (uint32_t)(ac); \
        (a) = MD5_ROTATE_LEFT((a), (s)); \
        (a) += (b); \
    } while (0)

#define MD5_HH(a, b, c, d, x, s, ac) \
    do { \
        (a) += MD5_H((b), (c), (d)) + (x) + (uint32_t)(ac); \
        (a) = MD5_ROTATE_LEFT((a), (s)); \
        (a) += (b); \
    } while (0)

#define MD5_II(a, b, c, d, x, s, ac) \
    do { \
        (a) += MD5_I((b), (c), (d)) + (x) + (uint32_t)(ac); \
        (a) = MD5_ROTATE_LEFT((a), (s)); \
        (a) += (b); \
    } while (0)

/*********************************************************************
 * @brief   MD5 核心处理函数（处理 64 字节数据块）
 * @param   state   MD5 状态寄存器（4 个 32 位字）
 * @param   block   64 字节数据块（小端序）
 * @note    实现 RFC 1321 标准的 MD5Transform 函数
 *          包含 4 轮 64 步变换操作，使用 FF/GG/HH/II 宏
 *********************************************************************/
static void md5_transform(uint32_t state[4], const uint8_t block[64])
{
    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t x[16];
    uint32_t i;

    /* 将字节块转换为 32 位字（小端序） */
    for (i = 0; i < 16; i++)
    {
        x[i] = (uint32_t)block[i * 4]
             | ((uint32_t)block[i * 4 + 1] << 8)
             | ((uint32_t)block[i * 4 + 2] << 16)
             | ((uint32_t)block[i * 4 + 3] << 24);
    }

    /* 第 1 轮 */
    MD5_FF(a, b, c, d, x[ 0],  7, 0xd76aa478);
    MD5_FF(d, a, b, c, x[ 1], 12, 0xe8c7b756);
    MD5_FF(c, d, a, b, x[ 2], 17, 0x242070db);
    MD5_FF(b, c, d, a, x[ 3], 22, 0xc1bdceee);
    MD5_FF(a, b, c, d, x[ 4],  7, 0xf57c0faf);
    MD5_FF(d, a, b, c, x[ 5], 12, 0x4787c62a);
    MD5_FF(c, d, a, b, x[ 6], 17, 0xa8304613);
    MD5_FF(b, c, d, a, x[ 7], 22, 0xfd469501);
    MD5_FF(a, b, c, d, x[ 8],  7, 0x698098d8);
    MD5_FF(d, a, b, c, x[ 9], 12, 0x8b44f7af);
    MD5_FF(c, d, a, b, x[10], 17, 0xffff5bb1);
    MD5_FF(b, c, d, a, x[11], 22, 0x895cd7be);
    MD5_FF(a, b, c, d, x[12],  7, 0x6b901122);
    MD5_FF(d, a, b, c, x[13], 12, 0xfd987193);
    MD5_FF(c, d, a, b, x[14], 17, 0xa679438e);
    MD5_FF(b, c, d, a, x[15], 22, 0x49b40821);

    /* 第 2 轮 */
    MD5_GG(a, b, c, d, x[ 1],  5, 0xf61e2562);
    MD5_GG(d, a, b, c, x[ 6],  9, 0xc040b340);
    MD5_GG(c, d, a, b, x[11], 14, 0x265e5a51);
    MD5_GG(b, c, d, a, x[ 0], 20, 0xe9b6c7aa);
    MD5_GG(a, b, c, d, x[ 5],  5, 0xd62f105d);
    MD5_GG(d, a, b, c, x[10],  9, 0x02441453);
    MD5_GG(c, d, a, b, x[15], 14, 0xd8a1e681);
    MD5_GG(b, c, d, a, x[ 4], 20, 0xe7d3fbc8);
    MD5_GG(a, b, c, d, x[ 9],  5, 0x21e1cde6);
    MD5_GG(d, a, b, c, x[14],  9, 0xc33707d6);
    MD5_GG(c, d, a, b, x[ 3], 14, 0xf4d50d87);
    MD5_GG(b, c, d, a, x[ 8], 20, 0x455a14ed);
    MD5_GG(a, b, c, d, x[13],  5, 0xa9e3e905);
    MD5_GG(d, a, b, c, x[ 2],  9, 0xfcefa3f8);
    MD5_GG(c, d, a, b, x[ 7], 14, 0x676f02d9);
    MD5_GG(b, c, d, a, x[12], 20, 0x8d2a4c8a);

    /* 第 3 轮 */
    MD5_HH(a, b, c, d, x[ 5],  4, 0xfffa3942);
    MD5_HH(d, a, b, c, x[ 8], 11, 0x8771f681);
    MD5_HH(c, d, a, b, x[11], 16, 0x6d9d6122);
    MD5_HH(b, c, d, a, x[14], 23, 0xfde5380c);
    MD5_HH(a, b, c, d, x[ 1],  4, 0xa4beea44);
    MD5_HH(d, a, b, c, x[ 4], 11, 0x4bdecfa9);
    MD5_HH(c, d, a, b, x[ 7], 16, 0xf6bb4b60);
    MD5_HH(b, c, d, a, x[10], 23, 0xbebfbc70);
    MD5_HH(a, b, c, d, x[13],  4, 0x289b7ec6);
    MD5_HH(d, a, b, c, x[ 0], 11, 0xeaa127fa);
    MD5_HH(c, d, a, b, x[ 3], 16, 0xd4ef3085);
    MD5_HH(b, c, d, a, x[ 6], 23, 0x04881d05);
    MD5_HH(a, b, c, d, x[ 9],  4, 0xd9d4d039);
    MD5_HH(d, a, b, c, x[12], 11, 0xe6db99e5);
    MD5_HH(c, d, a, b, x[15], 16, 0x1fa27cf8);
    MD5_HH(b, c, d, a, x[ 2], 23, 0xc4ac5665);

    /* 第 4 轮 */
    MD5_II(a, b, c, d, x[ 0],  6, 0xf4292244);
    MD5_II(d, a, b, c, x[ 7], 10, 0x432aff97);
    MD5_II(c, d, a, b, x[14], 15, 0xab9423a7);
    MD5_II(b, c, d, a, x[ 5], 21, 0xfc93a039);
    MD5_II(a, b, c, d, x[12],  6, 0x655b59c3);
    MD5_II(d, a, b, c, x[ 3], 10, 0x8f0ccc92);
    MD5_II(c, d, a, b, x[10], 15, 0xffeff47d);
    MD5_II(b, c, d, a, x[ 1], 21, 0x85845dd1);
    MD5_II(a, b, c, d, x[ 8],  6, 0x6fa87e4f);
    MD5_II(d, a, b, c, x[15], 10, 0xfe2ce6e0);
    MD5_II(c, d, a, b, x[ 6], 15, 0xa3014314);
    MD5_II(b, c, d, a, x[13], 21, 0x4e0811a1);
    MD5_II(a, b, c, d, x[ 4],  6, 0xf7537e82);
    MD5_II(d, a, b, c, x[11], 10, 0xbd3af235);
    MD5_II(c, d, a, b, x[ 2], 15, 0x2ad7d2bb);
    MD5_II(b, c, d, a, x[ 9], 21, 0xeb86d391);

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

/*********************************************************************
 * @brief   MD5 初始化（设置初始状态和计数器）
 * @param   ctx   MD5 上下文指针
 * @note    初始化 state[4] 为标准魔数，count[2] 清零
 *          必须在调用 my_md5_update() 之前调用
 *********************************************************************/
void my_md5_init(my_md5_ctx_t *ctx)
{
    if ((void *)ctx == 0U)
    {
        return;
    }

    ctx->count[0] = 0;
    ctx->count[1] = 0;

    /* 初始化 MD5 魔数 */
    ctx->state[0] = 0x67452301U;
    ctx->state[1] = 0xefcdab89U;
    ctx->state[2] = 0x98badcfeU;
    ctx->state[3] = 0x10325476U;
}

/*********************************************************************
 * @brief   MD5 更新（处理输入数据块，可多次调用）
 * @param   ctx   MD5 上下文指针
 * @param   data  输入数据缓冲区指针
 * @param   len   数据长度（字节）
 * @note    流式接口：可多次调用处理任意长度数据
 *          内部自动维护 64 字节缓冲区和比特计数器
 *          当缓冲区满 64 字节时自动调用 md5_transform()
 *********************************************************************/
void my_md5_update(my_md5_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t index;
    uint32_t part_len;
    uint32_t i;
    uint32_t t;

    if ((void *)ctx == 0U || (const void *)data == 0U)
    {
        return;
    }

    /* 计算已填充字节数 */
    index = (uint8_t)((ctx->count[0] >> 3) & 0x3F);

    /* 更新比特计数（低 32 位） */
    t = ctx->count[0];
    if ((ctx->count[0] += len << 3) < t)
    {
        ctx->count[1]++;  /* 进位到高 32 位 */
    }
    /* 更新比特计数（高 32 位） */
    ctx->count[1] += len >> 29;

    part_len = 64U - index;

    if (len >= part_len)
    {
        memcpy(&ctx->buffer[index], data, part_len);
        md5_transform(ctx->state, ctx->buffer);

        for (i = part_len; i + 64U <= len; i += 64U)
        {
            md5_transform(ctx->state, &data[i]);
        }

        index = 0U;
    }
    else
    {
        i = 0U;
    }

    /* 填充剩余数据到缓冲区 */
    if (i < len)
    {
        memcpy(&ctx->buffer[index], &data[i], len - i);
    }
}

/*********************************************************************
 * @brief   MD5 最终计算（填充数据并输出 16 字节摘要）
 * @param   ctx     MD5 上下文指针
 * @param   output  输出缓冲区（至少 16 字节）
 * @note    1. 保存 64 位比特计数（小端序）
 *          2. 填充 0x80 + 0x00... + 64位长度
 *          3. 输出 16 字节 MD5 摘要（小端序）
 *          此函数调用后，ctx 不应再继续使用
 *********************************************************************/
void my_md5_final(my_md5_ctx_t *ctx, uint8_t *output)
{
    uint8_t bits[8];
    uint32_t index;
    uint32_t pad_len;
    uint8_t padding[64];
    uint32_t i;

    if ((void *)ctx == 0U || (void *)output == 0U)
    {
        return;
    }

    /* 保存比特计数（小端序） */
    for (i = 0; i < 4; i++)
    {
        bits[i] = (uint8_t)(ctx->count[0] >> (i * 8));
    }
    for (i = 0; i < 4; i++)
    {
        bits[i + 4] = (uint8_t)(ctx->count[1] >> (i * 8));
    }

    /* 填充：0x80 + 若干个 0x00 */
    index = (uint8_t)((ctx->count[0] >> 3) & 0x3F);
    pad_len = (index < 56U) ? (56U - index) : (120U - index);

    memset(padding, 0, 64);
    padding[0] = 0x80;

    my_md5_update(ctx, padding, pad_len);

    /* 附加比特计数 */
    my_md5_update(ctx, bits, 8);

    /* 输出结果（小端序） */
    for (i = 0; i < 4; i++)
    {
        output[i]     = (uint8_t)(ctx->state[0] >> (i * 8));
        output[i + 4] = (uint8_t)(ctx->state[1] >> (i * 8));
        output[i + 8] = (uint8_t)(ctx->state[2] >> (i * 8));
        output[i + 12] = (uint8_t)(ctx->state[3] >> (i * 8));
    }
}

/*********************************************************************
 *  地理围栏工具实现
 *********************************************************************/

/*********************************************************************
 * @brief   Haversine 公式计算两点球面距离（米）
 * @param   lat1 点1纬度（微度）
 * @param   lon1 点1经度（微度）
 * @param   lat2 点2纬度（微度）
 * @param   lon2 点2经度（微度）
 * @return  两点间的球面距离（米）
 *********************************************************************/
static uint32_t calculate_distance_m(int32_t lat1, int32_t lon1,
                                      int32_t lat2, int32_t lon2)
{
    double rad_lat1;
    double rad_lat2;
    double d_lat;
    double d_lon;
    double a;
    double c;

    /* 微度 → 弧度 */
    rad_lat1 = (double)lat1 * D_MICRO_DEG_TO_RAD;
    rad_lat2 = (double)lat2 * D_MICRO_DEG_TO_RAD;
    d_lat    = rad_lat2 - rad_lat1;
    d_lon    = ((double)lon2 - (double)lon1) * D_MICRO_DEG_TO_RAD;

    /* Haversine 公式 */
    a = sin(d_lat / 2.0) * sin(d_lat / 2.0) +
        cos(rad_lat1) * cos(rad_lat2) *
        sin(d_lon / 2.0) * sin(d_lon / 2.0);

    c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));

    return (uint32_t)(D_EARTH_RADIUS_M * c);
}

/*********************************************************************
 * @brief   判断点是否在圆内
 * @param   lat         当前纬度（微度）
 * @param   lon         当前经度（微度）
 * @param   center_lat  圆心纬度（微度）
 * @param   center_lon  圆心经度（微度）
 * @param   radius      半径（米）
 * @return  true: 在圆内（含边界）  false: 在圆外
 * @note    使用 Haversine 公式计算球面距离后与半径比较
 *********************************************************************/
bool my_tool_in_circle(int32_t lat, int32_t lon,
                       int32_t center_lat, int32_t center_lon,
                       uint32_t radius)
{
    uint32_t distance;

    distance = calculate_distance_m(lat, lon, center_lat, center_lon);

    return (distance <= radius);
}

/*********************************************************************
 *  字节序/安全字符串工具
 *********************************************************************/

/*********************************************************************
 * @brief   大端字节数组转 uint16_t
 * @param   buf 指向 2 字节大端数据的指针
 * @return  转换后的 uint16_t 值
 * @note    示例：buf={0x01, 0x02} → 0x0102
 *********************************************************************/
uint16_t my_tool_be16_to_u16(const uint8_t *buf)
{
    if (buf == NULL)
    {
        return 0;
    }

    return (uint16_t)(((uint16_t)buf[0] << 8) | (uint16_t)buf[1]);
}

/*********************************************************************
 * @brief   uint16_t 转大端字节数组
 * @param   val  待转换的 uint16_t 值
 * @param   buf  输出缓冲区（至少 2 字节）
 * @note    示例：val=0x0102 → buf={0x01, 0x02}
 *********************************************************************/
void my_tool_u16_to_be16(uint16_t val, uint8_t *buf)
{
    if (buf == NULL)
    {
        return;
    }

    buf[0] = (uint8_t)((val >> 8) & 0xFFU);
    buf[1] = (uint8_t)(val & 0xFFU);
}

/*********************************************************************
 * @brief   大端 4 字节数组转 uint32_t
 * @param   buf 指向 4 字节大端数据的指针
 * @return  转换后的 uint32_t 值；buf 为空时返回 0
 * @note    示例：buf={0x01,0x02,0x03,0x04} → 0x01020304
 *********************************************************************/
uint32_t my_tool_be32_to_u32(const uint8_t *buf)
{
    if (buf == NULL)
    {
        return 0;
    }

    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8)  | (uint32_t)buf[3];
}

/*********************************************************************
 * @brief   uint32_t 转大端 4 字节数组
 * @param   val  待转换的 uint32_t 值
 * @param   buf  输出缓冲区（至少 4 字节）
 * @note    示例：val=0x01020304 → buf={0x01,0x02,0x03,0x04}
 *********************************************************************/
void my_tool_u32_to_be32(uint32_t val, uint8_t *buf)
{
    if (buf == NULL)
    {
        return;
    }

    buf[0] = (uint8_t)((val >> 24) & 0xFFU);
    buf[1] = (uint8_t)((val >> 16) & 0xFFU);
    buf[2] = (uint8_t)((val >> 8) & 0xFFU);
    buf[3] = (uint8_t)(val & 0xFFU);
}

/*********************************************************************
 * @brief   安全字符串拷贝（保证 '\0' 终止）
 * @param   dest      目标缓冲区
 * @param   dest_size 目标缓冲区容量
 * @param   src       源字符串
 * @return  dest 指针
 * @note    无论 src 多长，dest[dest_size-1] 始终为 '\0'
 *********************************************************************/
char *my_tool_strncpy(char *dest, size_t dest_size, const char *src)
{
    size_t i;

    if (dest == NULL || dest_size == 0)
    {
        return dest;
    }

    if (src == NULL)
    {
        dest[0] = '\0';
        return dest;
    }

    for (i = 0; i < dest_size - 1 && src[i] != '\0'; i++)
    {
        dest[i] = src[i];
    }

    dest[i] = '\0';
    return dest;
}
