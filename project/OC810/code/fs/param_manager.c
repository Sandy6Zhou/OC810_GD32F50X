/*******************************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       param_manager.c
**文件描述：       参数管理系统核心实现
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.06.02
*******************************************************************************
** 功能描述：       1. 实现参数的读写删除等操作
**                 2. 基于LittleFS文件系统
**                 3. 支持CRC32校验和魔数验证
*******************************************************************************/

#include "param_manager.h"
#include "param_config.h"
#include "lfs_config.h"
#include "lfs.h"
#include "my_log.h"
#include <stdio.h>
#include <string.h>

/*******************************************************************************
 * 内部变量
 ******************************************************************************/

static bool s_initialized = false;

/*******************************************************************************
 * 内部辅助函数
 ******************************************************************************/

/*******************************************************************************
 * @brief   CRC32计算（标准多项式 0xEDB88320，逐字节计算，节省FLASH）
 * @param   data    数据指针
 * @param   len     数据长度
 * @return  CRC32值
 ******************************************************************************/
static uint32_t param_crc32_calculate(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    uint32_t i;
    uint8_t j;

    if (data == NULL || len == 0)
    {
        return 0;
    }

    for (i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (j = 0; j < 8; j++)
        {
            if (crc & 1)
            {
                crc = (crc >> 1) ^ 0xEDB88320UL;
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return (crc ^ 0xFFFFFFFF);
}

/*******************************************************************************
 * @brief   参数ID转换为文件名
 * @param   param_id    参数ID
 * @param   filename    输出文件名缓冲区
 * @param   len         缓冲区长度
 ******************************************************************************/
static void param_id_to_filename(uint16_t param_id, char *filename, size_t len)
{
    const char *category;

    if (param_id >= PARAM_ID_SYS_BASE && param_id <= PARAM_ID_SYS_MAX)
    {
        category = PARAM_CAT_SYS;
    }
    else if (param_id >= PARAM_ID_NET_BASE && param_id <= PARAM_ID_NET_MAX)
    {
        category = PARAM_CAT_NET;
    }
    else if (param_id >= PARAM_ID_DEV_BASE && param_id <= PARAM_ID_DEV_MAX)
    {
        category = PARAM_CAT_DEV;
    }
    else if (param_id >= PARAM_ID_USR_BASE && param_id <= PARAM_ID_USR_MAX)
    {
        category = PARAM_CAT_USR;
    }
    else if (param_id >= PARAM_ID_CAL_BASE && param_id <= PARAM_ID_CAL_MAX)
    {
        category = PARAM_CAT_CAL;
    }
    else
    {
        category = PARAM_CAT_RSV;
    }

    snprintf(filename, len, PARAM_FILE_FMT, category, param_id & 0x0FFF);
}

/*******************************************************************************
 * @brief   构造参数头部
 * @param   header      头部缓冲区
 * @param   param_id    参数ID
 * @param   data        数据指针
 * @param   data_len    数据长度
 ******************************************************************************/
static void param_build_header(param_header_t *header, uint16_t param_id,
                               const uint8_t *data, uint16_t data_len)
{
    header->magic = PARAM_MAGIC;
    header->version = PARAM_VERSION;
    header->param_id = param_id;
    header->data_len = data_len;
    header->crc32 = param_crc32_calculate(data, data_len);
}

/*******************************************************************************
 * @brief   验证参数头部有效性
 * @param   header      头部指针
 * @param   param_id    期望的参数ID
 * @return  PARAM_OK: 有效，<0: 无效
 ******************************************************************************/
static int param_verify_header(const param_header_t *header, uint16_t param_id)
{
    if (header->magic != PARAM_MAGIC)
    {
        MY_LOG_W("[PARAM] Magic mismatch: 0x%04X", header->magic);
        return PARAM_ERR_MAGIC;
    }

    if (header->param_id != param_id)
    {
        MY_LOG_W("[PARAM] ID mismatch: expect 0x%04X, got 0x%04X",
                 param_id, header->param_id);
        return PARAM_ERR_FORMAT;
    }

    if (header->data_len == 0 || header->data_len > (LFS_FILE_MAX - sizeof(param_header_t)))
    {
        MY_LOG_W("[PARAM] Invalid data_len: %d", header->data_len);
        return PARAM_ERR_SIZE;
    }

    return PARAM_OK;
}

/*******************************************************************************
 * 公开API实现
 ******************************************************************************/

/*******************************************************************************
 * @brief   初始化参数管理系统
 * @return  PARAM_OK: 成功，其他: 错误码
 * @note    初始化LittleFS，创建参数目录
 ******************************************************************************/
int param_manager_init(void)
{
    int ret;
    lfs_dir_t dir;

    if (s_initialized)
    {
        return PARAM_OK;
    }

    /* 初始化LittleFS移植层 */
    ret = lfs_port_init();
    if (ret != 0)
    {
        MY_LOG_E("[PARAM] lfs_port_init failed: %d", ret);
        return PARAM_ERR_INIT;
    }

    /* 挂载文件系统 */
    ret = lfs_port_mount();
    if (ret == LFS_ERR_CORRUPT)
    {
        /* 首次使用或文件系统损坏，格式化后重新挂载 */
        MY_LOG_W("[PARAM] Filesystem corrupt or not formatted, formatting...");
        ret = lfs_port_format();
        if (ret != 0)
        {
            MY_LOG_E("[PARAM] lfs_port_format failed: %d", ret);
            return PARAM_ERR_INIT;
        }
        ret = lfs_port_mount();
        if (ret != 0)
        {
            MY_LOG_E("[PARAM] lfs_port_mount after format failed: %d", ret);
            return PARAM_ERR_INIT;
        }
    }
    else if (ret != 0)
    {
        MY_LOG_E("[PARAM] lfs_port_mount failed: %d", ret);
        return PARAM_ERR_INIT;
    }

    /* 创建params目录 */
    ret = lfs_dir_open(&g_lfs_handle.lfs, &dir, PARAM_DIR_PATH);
    if (ret == LFS_ERR_NOENT)
    {
        /* 目录不存在，创建 */
        ret = lfs_mkdir(&g_lfs_handle.lfs, PARAM_DIR_PATH);
        if (ret != 0)
        {
            MY_LOG_E("[PARAM] mkdir %s failed: %d", PARAM_DIR_PATH, ret);
            return PARAM_ERR_INIT;
        }
    }
    else if (ret == 0)
    {
        /* 目录已存在，关闭 */
        lfs_dir_close(&g_lfs_handle.lfs, &dir);
    }
    else
    {
        MY_LOG_E("[PARAM] dir_open %s failed: %d", PARAM_DIR_PATH, ret);
        return PARAM_ERR_INIT;
    }

    s_initialized = true;
    MY_LOG_I("[PARAM] Init OK, dir=%s", PARAM_DIR_PATH);

    return PARAM_OK;
}

/*******************************************************************************
 * @brief   反初始化参数管理系统
 * @return  PARAM_OK: 成功，其他: 错误码
 ******************************************************************************/
int param_manager_deinit(void)
{
    if (!s_initialized)
    {
        return PARAM_OK;
    }

    /* 卸载文件系统 */
    lfs_port_unmount();

    /* 反初始化移植层 */
    lfs_port_deinit();

    s_initialized = false;

    return PARAM_OK;
}

/*******************************************************************************
 * @brief   读取参数
 * @param   param_id    参数ID
 * @param   buf         输出缓冲区
 * @param   len         缓冲区长度
 * @return  实际读取字节数，<0: 错误码
 ******************************************************************************/
int param_read(uint16_t param_id, void *buf, uint16_t len)
{
    char filename[64];
    lfs_file_t file;
    param_header_t header;
    int ret;
    lfs_ssize_t read_size;
    uint32_t calc_crc;

    if (!s_initialized)
    {
        return PARAM_ERR_INIT;
    }

    if (!g_lfs_handle.mounted)
    {
        return PARAM_ERR_INIT;
    }

    if (buf == NULL || len == 0)
    {
        return PARAM_ERR_SIZE;
    }

    param_id_to_filename(param_id, filename, sizeof(filename));

    ret = lfs_file_open(&g_lfs_handle.lfs, &file, filename, LFS_O_RDONLY);
    if (ret != 0)
    {
        return PARAM_ERR_NOT_FOUND;
    }

    /* 读取头部 */
    read_size = lfs_file_read(&g_lfs_handle.lfs, &file, &header, sizeof(param_header_t));
    if (read_size != (lfs_ssize_t)sizeof(param_header_t))
    {
        lfs_file_close(&g_lfs_handle.lfs, &file);
        return PARAM_ERR_FORMAT;
    }

    /* 验证头部 */
    ret = param_verify_header(&header, param_id);
    if (ret != PARAM_OK)
    {
        lfs_file_close(&g_lfs_handle.lfs, &file);
        return ret;
    }

    /* 检查缓冲区是否足够 */
    if (len < header.data_len)
    {
        lfs_file_close(&g_lfs_handle.lfs, &file);
        return PARAM_ERR_SIZE;
    }

    /* 读取数据 */
    read_size = lfs_file_read(&g_lfs_handle.lfs, &file, buf, header.data_len);
    if (read_size != (lfs_ssize_t)header.data_len)
    {
        lfs_file_close(&g_lfs_handle.lfs, &file);
        return PARAM_ERR_FILE;
    }

    /* 验证CRC32 */
    calc_crc = param_crc32_calculate((const uint8_t *)buf, header.data_len);
    if (calc_crc != header.crc32)
    {
        MY_LOG_W("[PARAM] CRC mismatch: expect 0x%08X, got 0x%08X",
                 header.crc32, calc_crc);
        lfs_file_close(&g_lfs_handle.lfs, &file);
        return PARAM_ERR_CRC;
    }

    lfs_file_close(&g_lfs_handle.lfs, &file);

    return (int)header.data_len;
}

/*******************************************************************************
 * @brief   写入参数
 * @param   param_id    参数ID
 * @param   buf         数据缓冲区
 * @param   len         数据长度
 * @return  PARAM_OK: 成功，<0: 错误码
 ******************************************************************************/
int param_write(uint16_t param_id, const void *buf, uint16_t len)
{
    char filename[64];
    lfs_file_t file;
    param_header_t header;
    int ret;
    lfs_ssize_t write_size;
    uint16_t max_data_len;

    if (!s_initialized)
    {
        return PARAM_ERR_INIT;
    }

    if (!g_lfs_handle.mounted)
    {
        return PARAM_ERR_INIT;
    }

    if (buf == NULL || len == 0)
    {
        return PARAM_ERR_SIZE;
    }

    /* 检查数据长度不超过限制 */
    max_data_len = (uint16_t)(LFS_FILE_MAX - sizeof(param_header_t));
    if (len > max_data_len)
    {
        MY_LOG_E("[PARAM] Data too large: %d > %d", len, max_data_len);
        return PARAM_ERR_SIZE;
    }

    param_id_to_filename(param_id, filename, sizeof(filename));

    /* 构造头部 */
    param_build_header(&header, param_id, (const uint8_t *)buf, len);

    /* 打开文件（创建/截断） */
    ret = lfs_file_open(&g_lfs_handle.lfs, &file, filename,
                        LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (ret != 0)
    {
        MY_LOG_E("[PARAM] file_open %s failed: %d", filename, ret);
        return PARAM_ERR_FILE;
    }

    /* 写入头部 */
    write_size = lfs_file_write(&g_lfs_handle.lfs, &file, &header, sizeof(param_header_t));
    if (write_size != (lfs_ssize_t)sizeof(param_header_t))
    {
        lfs_file_close(&g_lfs_handle.lfs, &file);
        return PARAM_ERR_FILE;
    }

    /* 写入数据 */
    write_size = lfs_file_write(&g_lfs_handle.lfs, &file, buf, len);
    if (write_size != (lfs_ssize_t)len)
    {
        lfs_file_close(&g_lfs_handle.lfs, &file);
        return PARAM_ERR_FILE;
    }

    /* 同步确保数据落盘 */
    ret = lfs_file_sync(&g_lfs_handle.lfs, &file);
    if (ret != 0)
    {
        MY_LOG_W("[PARAM] file_sync failed: %d", ret);
    }

    lfs_file_close(&g_lfs_handle.lfs, &file);

    return PARAM_OK;
}

/*******************************************************************************
 * @brief   删除参数
 * @param   param_id    参数ID
 * @return  PARAM_OK: 成功，<0: 错误码
 ******************************************************************************/
int param_delete(uint16_t param_id)
{
    char filename[64];
    int ret;

    if (!s_initialized)
    {
        return PARAM_ERR_INIT;
    }

    if (!g_lfs_handle.mounted)
    {
        return PARAM_ERR_INIT;
    }

    param_id_to_filename(param_id, filename, sizeof(filename));

    ret = lfs_remove(&g_lfs_handle.lfs, filename);
    if (ret != 0)
    {
        return PARAM_ERR_NOT_FOUND;
    }

    return PARAM_OK;
}

/*******************************************************************************
 * @brief   检查参数是否存在
 * @param   param_id    参数ID
 * @return  1: 存在，0: 不存在，<0: 错误
 ******************************************************************************/
int param_exists(uint16_t param_id)
{
    char filename[64];
    struct lfs_info info;
    int ret;

    if (!s_initialized)
    {
        return PARAM_ERR_INIT;
    }

    if (!g_lfs_handle.mounted)
    {
        return PARAM_ERR_INIT;
    }

    param_id_to_filename(param_id, filename, sizeof(filename));

    ret = lfs_stat(&g_lfs_handle.lfs, filename, &info);
    if (ret == 0)
    {
        return 1;  /* 存在 */
    }

    return 0;  /* 不存在 */
}

/*******************************************************************************
 * @brief   获取参数大小
 * @param   param_id    参数ID
 * @return  参数字节数，<0: 错误码
 ******************************************************************************/
int param_get_size(uint16_t param_id)
{
    char filename[64];
    lfs_file_t file;
    param_header_t header;
    int ret;
    lfs_ssize_t read_size;

    if (!s_initialized)
    {
        return PARAM_ERR_INIT;
    }

    if (!g_lfs_handle.mounted)
    {
        return PARAM_ERR_INIT;
    }

    param_id_to_filename(param_id, filename, sizeof(filename));

    ret = lfs_file_open(&g_lfs_handle.lfs, &file, filename, LFS_O_RDONLY);
    if (ret != 0)
    {
        return PARAM_ERR_NOT_FOUND;
    }

    /* 仅读取头部获取数据长度 */
    read_size = lfs_file_read(&g_lfs_handle.lfs, &file, &header, sizeof(param_header_t));
    lfs_file_close(&g_lfs_handle.lfs, &file);

    if (read_size != (lfs_ssize_t)sizeof(param_header_t))
    {
        return PARAM_ERR_FORMAT;
    }

    if (header.magic != PARAM_MAGIC)
    {
        return PARAM_ERR_MAGIC;
    }

    return (int)header.data_len;
}

/*******************************************************************************
 * @brief   清空所有参数
 * @return  PARAM_OK: 成功，<0: 错误码
 * @note    删除params目录下所有文件
 ******************************************************************************/
int param_clear_all(void)
{
    lfs_dir_t dir;
    struct lfs_info info;
    char filepath[80];
    int ret;
    uint32_t count = 0;

    if (!s_initialized)
    {
        return PARAM_ERR_INIT;
    }

    if (!g_lfs_handle.mounted)
    {
        return PARAM_ERR_INIT;
    }

    ret = lfs_dir_open(&g_lfs_handle.lfs, &dir, PARAM_DIR_PATH);
    if (ret != 0)
    {
        return PARAM_ERR_FILE;
    }

    while (1)
    {
        ret = lfs_dir_read(&g_lfs_handle.lfs, &dir, &info);
        if (ret <= 0)
        {
            break;
        }

        /* 跳过 . 和 .. */
        if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0)
        {
            continue;
        }

        /* 构造完整路径并删除 */
        snprintf(filepath, sizeof(filepath), "%s/%s", PARAM_DIR_PATH, info.name);
        ret = lfs_remove(&g_lfs_handle.lfs, filepath);
        if (ret != 0)
        {
            MY_LOG_W("[PARAM] Failed to remove %s: %d", filepath, ret);
        }
        else
        {
            count++;
        }
    }

    lfs_dir_close(&g_lfs_handle.lfs, &dir);

    MY_LOG_I("[PARAM] Cleared %d params", count);

    return PARAM_OK;
}

/*******************************************************************************
 * @brief   验证参数数据完整性
 * @return  PARAM_OK: 完整，<0: 损坏
 ******************************************************************************/
int param_verify_integrity(void)
{
    lfs_dir_t dir;
    struct lfs_info info;
    char filepath[80];
    int ret;
    uint32_t total = 0;
    uint32_t corrupt = 0;

    if (!s_initialized)
    {
        return PARAM_ERR_INIT;
    }

    if (!g_lfs_handle.mounted)
    {
        return PARAM_ERR_INIT;
    }

    ret = lfs_dir_open(&g_lfs_handle.lfs, &dir, PARAM_DIR_PATH);
    if (ret != 0)
    {
        return PARAM_ERR_FILE;
    }

    while (1)
    {
        ret = lfs_dir_read(&g_lfs_handle.lfs, &dir, &info);
        if (ret <= 0)
        {
            break;
        }

        /* 跳过 . 和 .. */
        if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0)
        {
            continue;
        }

        /* 跳过目录 */
        if (info.type == LFS_TYPE_DIR)
        {
            continue;
        }

        total++;
        snprintf(filepath, sizeof(filepath), "%s/%s", PARAM_DIR_PATH, info.name);

        /* 读取完整数据并验证CRC32 */
        lfs_file_t file;
        param_header_t header;
        uint8_t data_buf[LFS_CACHE_SIZE];
        uint32_t calc_crc = 0xFFFFFFFF;
        uint16_t remaining;

        ret = lfs_file_open(&g_lfs_handle.lfs, &file, filepath, LFS_O_RDONLY);
        if (ret != 0)
        {
            corrupt++;
            continue;
        }

        lfs_ssize_t read_size = lfs_file_read(&g_lfs_handle.lfs, &file,
                                               &header, sizeof(param_header_t));
        if (read_size != (lfs_ssize_t)sizeof(param_header_t) ||
            header.magic != PARAM_MAGIC ||
            header.data_len == 0 ||
            header.data_len > (LFS_FILE_MAX - sizeof(param_header_t)))
        {
            lfs_file_close(&g_lfs_handle.lfs, &file);
            corrupt++;
            MY_LOG_W("[PARAM] Corrupt header: %s", filepath);
            continue;
        }

        /* 逐块读取完整数据，增量计算CRC32 */
        remaining = header.data_len;
        while (remaining > 0)
        {
            uint16_t chunk = (remaining > LFS_CACHE_SIZE) ? LFS_CACHE_SIZE : remaining;

            read_size = lfs_file_read(&g_lfs_handle.lfs, &file, data_buf, chunk);
            if (read_size != (lfs_ssize_t)chunk)
            {
                lfs_file_close(&g_lfs_handle.lfs, &file);
                corrupt++;
                MY_LOG_W("[PARAM] Corrupt data: %s", filepath);
                goto next_file;
            }

            /* 增量CRC计算（与param_crc32_calculate算法一致） */
            for (uint16_t k = 0; k < chunk; k++)
            {
                calc_crc ^= data_buf[k];
                for (uint8_t b = 0; b < 8; b++)
                {
                    if (calc_crc & 1)
                    {
                        calc_crc = (calc_crc >> 1) ^ 0xEDB88320UL;
                    }
                    else
                    {
                        calc_crc >>= 1;
                    }
                }
            }

            remaining -= chunk;
        }

        lfs_file_close(&g_lfs_handle.lfs, &file);
        calc_crc ^= 0xFFFFFFFF;

        if (calc_crc != header.crc32)
        {
            corrupt++;
            MY_LOG_W("[PARAM] CRC mismatch: %s (expect 0x%08X, got 0x%08X)",
                     filepath, header.crc32, calc_crc);
        }
        continue;

next_file:
        continue;
    }

    lfs_dir_close(&g_lfs_handle.lfs, &dir);

    if (corrupt > 0)
    {
        MY_LOG_E("[PARAM] Integrity check: %d/%d corrupt", corrupt, total);
        return PARAM_ERR_CRC;
    }

    MY_LOG_I("[PARAM] Integrity check: %d/%d OK", total, total);
    return PARAM_OK;
}
