/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       my_bl_utils.c
**文件描述：       Bootloader工具函数实现文件
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.06.22
*********************************************************************
** 功能描述：       1. CRC32位运算法实现
**                 2. CRC16-CCITT查表法实现
**                 3. MD5流式计算实现（RFC 1321标准）
**                 4. 系统复位与延时函数
*********************************************************************/

#include "my_bl.h"
#include <string.h>

/*********************************************************************
 * MD5 内部实现
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
 *          必须在调用 my_bl_md5_update() 之前调用
 *********************************************************************/
void my_bl_md5_init(my_bl_md5_ctx_t *ctx)
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
void my_bl_md5_update(my_bl_md5_ctx_t *ctx, const uint8_t *data, uint32_t len)
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
void my_bl_md5_final(my_bl_md5_ctx_t *ctx, uint8_t *output)
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

    my_bl_md5_update(ctx, padding, pad_len);

    /* 附加比特计数 */
    my_bl_md5_update(ctx, bits, 8);

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
 * @brief   CRC16-CCITT 计算（查表法）
 * @param   data  数据缓冲区指针
 * @param   len   数据长度（字节），支持最大 4GB
 * @return  CRC16 校验值；data 为空时返回 0
 * @note    固定 CCITT 多项式 0x1021，初始值 0xFFFF，输出取反
 *          使用 256 项预计算表，每字节只需一次查表+异或操作
 *********************************************************************/
uint16_t my_bl_crc16_table(const uint8_t *data, uint32_t len)
{
    uint16_t fcs = 0xFFFFU;  /* 初始值 0xFFFF */
    uint32_t i;

    if ((const void *)data == 0U)
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
 * @brief   执行系统复位
 * @param   None
 * @return  None
 * @note    调用 NVIC_SystemReset()，该函数不会返回；末尾 while(1) 为防御性代码
 *********************************************************************/
void my_bl_system_reset(void)
{
    /* 使用NVIC系统复位 */
    NVIC_SystemReset();

    /* 不应到达此处 */
    while (1U)
    {
    }
}
