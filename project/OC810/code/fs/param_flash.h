/*******************************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       param_flash.h
**文件描述：       FLASH分区定义与全分区读写擦除驱动接口
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.06.02
*******************************************************************************
** 功能描述：
** 1. 定义全量FLASH分区布局(BootLoader/app/setting/mcu_secondary/factory/bootconf)
** 2. 封装FMC底层操作，提供全分区读写擦除统一接口
** 3. 适配LittleFS block device回调（仅setting_storage分区）
** 4. 编译期_Static_assert校验分区合法性（对齐/完整/不跨Bank）
**
** 分区注意事项：
** 1. GD32单片机的不同BANK，BLOCK大小不同，BANK0为2KB页，BANK1为4KB页，
** 2. 分区尽量不要跨BANK，app可以除外
** 3. mcuboot操作升级时对app分区的读写需要注意不同BANK的BLOCK大小,确保读写正确
** 4. 实在要跨BANK时还要考虑到跨BANK前的SIZE必须是BLOCK_SIZE的整数倍，跨BANK后的SIZE也必须是BLOCK_SIZE的整数倍
** 5. 每个分区必须是BLOCK_SIZE的整数倍
** 6. 确保setting_storage放在零等待区，提高文件系统性能
** 7. 保证mcuboot + setting_storage = 256KB
** 8. 如果mcuboot分配区过小，可以考虑减少setting_storage大小
** 9. mcuboot采用裸机YMODEM方案，从视频模块升级，不支持网络
*******************************************************************************/

#ifndef __PARAM_FLASH_H__
#define __PARAM_FLASH_H__

#include <stdint.h>
#include <stdbool.h>

/*******************************************************************************
 * FLASH分区配置
 ******************************************************************************/

/*******************************************************************************
 * GD32F505VGT7 Internal FLASH 分区布局 (1MB = 1024KB)
 *******************************************************************************
 * 【用户FLASH区】（可编程擦除，总计1MB）
 * Bank0 (0x08000000~0x0807FFFF): 512KB, 256扇区×2KB, 前256KB零等待
 * Bank1 (0x08080000~0x080FFFFF): 512KB, 128扇区×4KB
 *******************************************************************************
 *
 * 【调试阶段分区布局】（PARAM_DEBUG_MODE=1，BootLoader实现前）
 *******************************************************************************
 * 地址范围                大小        分区名称           用途               文件系统
 * 0x08000000~0x0805CFFF  372KB       app               主应用（调试）       无
 * 0x080C7000~0x080FAFFF  208KB       setting_storage   参数存储（调试）     LittleFS ← 本模块
 * 0x080FB000~0x080FFFFF  20KB        factory_storage   工厂参数（调试）     无
 * (bootconf 0x080FF000~0x080FFFFF 4KB 包含在factory_storage末尾)
 * (mcu_secondary调试时与setting_storage重叠，调试不使用OTA)
 *******************************************************************************
 *
 * 【正式阶段分区布局】（PARAM_DEBUG_MODE=0，BootLoader实现后）
 *******************************************************************************
 * 地址范围                大小        分区名称           用途               文件系统
 * 0x08000000~0x0800BFFF  48KB        mcuboot           项目BootLoader      无
 * 0x0800C000~0x0803FFFF  208KB       setting_storage   参数存储            LittleFS ← 本模块
 * 0x08040000~0x0809CFFF  372KB       app               主应用程序          无
 * 0x0809D000~0x080FAFFF  372KB       mcu_secondary     OTA固件/AGPS星历    无
 * 0x080FB000~0x080FFFFF  20KB        factory_storage   工厂参数/IMEI/MAC   无
 * 0x080FF000~0x080FFFFF  4KB         bootconf          启动配置/OTA标志    (factory_storage末尾)
 *******************************************************************************
 *
 * 【重要说明】
 *******************************************************************************
 * - 原厂BootLoader: 0x1FFFB000~0x1FFFF7FF (18KB, 系统ROM, 独立地址空间)
 * - 用户FLASH总计: 1MB (Bank0 512KB + Bank1 512KB)
 * - Bank0 (0x08000000~0x0807FFFF): 512KB, 256扇区×2KB, 前256KB零等待
 * - Bank1 (0x08080000~0x080FFFFF): 512KB, 128扇区×4KB
 * - 调试阶段：APP在0x08000000，LittleFS在0x080C7000（Bank1），互不冲突
 * - 正式阶段：setting_storage在Bank0零等待区，100%性能最优
 *******************************************************************************
 *
 * 【setting_storage 分区详情（本模块使用）】
 *******************************************************************************
 * 调试阶段：setting_storage (208KB = 52扇区 × 4KB) - Bank1
 * 正式阶段：setting_storage (208KB = 104扇区 × 2KB) - Bank0零等待区
 * 起始地址: 0x080C7000 (调试) / 0x0800C000 (正式)
 * 结束地址: 0x080FAFFF (调试) / 0x0803FFFF (正式)
 * 扇区大小: 4KB (调试，Bank1硬件页) / 2KB (正式，Bank0硬件页)
 * 文件系统: LittleFS v2.11.3
 *******************************************************************************
 * 用途: 系统配置、网络参数、设备参数、用户设置、校准数据、GPS离线缓存等
 * 特性: 掉电安全、磨损均衡、FOTA隔离、零等待性能（正式阶段）
 * 寿命: >47年（公式: 104扇区×10K寿命/(年4380次×K), K=5为保守值）
 * GPS策略: RAM缓存50条(5KB) + FLASH保存最后10条(1KB)，每2小时刷新
 *******************************************************************************/

/*******************************************************************************
 * 调试阶段开关（BootLoader实现前使用）
 * *****************************************************************************
 * PARAM_DEBUG_MODE = 1: 调试阶段
 *   - APP编译地址：0x08000000（覆盖mcuboot和setting_storage）
 *   - setting_storage地址：0x080C7000（factory_storage之前208KB）
 *   - 用途：快速验证FLASH和LittleFS功能，无需BootLoader
 *
 * PARAM_DEBUG_MODE = 0: 正式阶段（BootLoader实现后）
 *   - APP编译地址：0x08040000（setting_storage之后）
 *   - setting_storage地址：0x0800C000（Bank0零等待区）
 *   - 用途：量产版本，完整FLASH分区布局
 ******************************************************************************/

#define PARAM_DEBUG_MODE        1               /**< 1=调试阶段，0=正式阶段 */

#if PARAM_DEBUG_MODE
    /* 调试阶段：放在factory_storage(0x080FB000)之前208KB */
    /* 0x080FB000 - 208KB = 0x080FB000 - 0x34000 = 0x080C7000 */
    #define PARAM_PARTITION_SETTING_BASE            0x080C7000UL    /**< 调试阶段使用Bank1 */
    #define PARAM_PARTITION_SETTING_SIZE            (208 * 1024UL)  /**< 208KB */
    #define PARAM_PARTITION_SETTING_SECTOR_SIZE     (4 * 1024UL)    /**< 4KB/扇区（Bank1硬件页大小） */
    #define PARAM_PARTITION_SETTING_SECTOR_COUNT    52              /**< 52个扇区（208KB / 4KB） */
    /* 地址范围：0x080C7000~0x080FAFFF */
#else
    /* 正式阶段：使用Bank0零等待区 */
    #define PARAM_PARTITION_SETTING_BASE            0x0800C000UL    /**< setting_storage起始地址 - Bank0零等待区 */
    #define PARAM_PARTITION_SETTING_SIZE            (208 * 1024UL)  /**< 208KB */
    #define PARAM_PARTITION_SETTING_SECTOR_SIZE     (2 * 1024UL)    /**< 2KB/扇区（Bank0硬件页大小） */
    #define PARAM_PARTITION_SETTING_SECTOR_COUNT    104             /**< 104个扇区（208KB / 2KB） */
#endif

/* Bank边界地址（Bank0: 0x08000000~0x0807FFFF, Bank1: 0x08080000~0x080FFFFF） */
#define PARAM_FLASH_BANK0_END_ADDR  0x08080000UL    /**< Bank0结束地址（Bank1起始） */

/* Bank硬件页大小（用于分区校验） */
#define PARAM_FLASH_BANK0_PAGE_SIZE (2 * 1024UL)    /**< Bank0硬件页大小 2KB */
#define PARAM_FLASH_BANK1_PAGE_SIZE (4 * 1024UL)    /**< Bank1硬件页大小 4KB */

/*******************************************************************************
 * 全量分区定义（调试/正式模式共用，仅setting_storage和app地址不同）
 *******************************************************************************
 * 每个分区包含: _BASE(起始地址) + _SIZE(大小)
 * 编译期校验确保：起始地址对齐、大小为扇区整数倍、不跨Bank（app除外）
 ******************************************************************************/

/* === Bank0 分区 === */

/* mcuboot: BootLoader（正式: 48KB, 调试: 0KB） */
#define PARAM_PARTITION_MCUBOOT_BASE    0x08000000UL
#define PARAM_PARTITION_MCUBOOT_SIZE    (48 * 1024UL)

/* app主应用与mcu_secondary等大，OTA镜像
 * 注意：
 *    跨Bank分区！Bank0(2KB页) + Bank1(4KB页)
 *    由Bootloader单独处理擦写，不纳入param_flash管理
 * */
#if PARAM_DEBUG_MODE
    #define PARAM_PARTITION_APP_BASE    0x08000000UL    /**< 调试: 无mcuboot，app从0起始 */
#else
    #define PARAM_PARTITION_APP_BASE    0x08040000UL    /**< 正式: mcuboot之后 */
#endif
#define PARAM_PARTITION_APP_SIZE    (376 * 1024UL)      /**< 始终376KB */

/* === Bank1 / 跨Bank分区 === */

/* mcu_secondary: OTA固件/AGPS星历（372KB，与app等大，OTA镜像）
 * 由Bootloader管理擦写，使用2KB对齐（兼顾Bank0/Bank1最小页大小）
 * 调试模式下与setting_storage重叠（调试不使用OTA） */
#if PARAM_DEBUG_MODE
    #define PARAM_PARTITION_MCU_SEC_BASE    0x080C7000UL  /**< 调试: 与setting_storage重叠 */
#else
    #define PARAM_PARTITION_MCU_SEC_BASE    0x0809E000UL  /**< 正式: app之后 */
#endif
#define PARAM_PARTITION_MCU_SEC_SIZE    (376 * 1024UL)

/* factory_storage: 工厂参数/IMEI/MAC（20KB） */
#define PARAM_PARTITION_FACTORY_BASE    0x080FB000UL
#define PARAM_PARTITION_FACTORY_SIZE    (12 * 1024UL)

/* bootconf: 启动配置/OTA标志（4KB） */
#define PARAM_PARTITION_BOOTCONF_BASE   0x080FE000UL
#define PARAM_PARTITION_BOOTCONF_SIZE   (4 * 1024UL)

/*******************************************************************************
 * 编译期分区合法性校验（_Static_assert: C11静态断言，编译时即报错）
 *******************************************************************************
 * 检查项（每个非跨Bank分区）：
 *   1. 起始地址必须扇区对齐（按所在Bank的硬件页大小）
 *   2. 分区大小必须是扇区整数倍（不允许不完整Block）
 *   3. 分区不得跨越Bank0/Bank1边界（0x08080000）
 *      Bank0和Bank1页大小不同（2KB vs 4KB），跨区会导致擦除/编程异常
 ******************************************************************************/

/* --- setting_storage（本模块管理） --- */
_Static_assert(
    (PARAM_PARTITION_SETTING_BASE % PARAM_PARTITION_SETTING_SECTOR_SIZE) == 0,
    "setting_storage: base address must be sector-aligned"
);
_Static_assert(
    (PARAM_PARTITION_SETTING_SIZE % PARAM_PARTITION_SETTING_SECTOR_SIZE) == 0,
    "setting_storage: size must be a multiple of sector size"
);
_Static_assert(
    ((PARAM_PARTITION_SETTING_BASE + PARAM_PARTITION_SETTING_SIZE) <= PARAM_FLASH_BANK0_END_ADDR) ||
    (PARAM_PARTITION_SETTING_BASE >= PARAM_FLASH_BANK0_END_ADDR),
    "setting_storage: must NOT cross Bank0/Bank1 boundary (0x08080000)"
);

/* --- mcuboot（Bank0, 2KB页） --- */
_Static_assert(
    (PARAM_PARTITION_MCUBOOT_BASE % PARAM_FLASH_BANK0_PAGE_SIZE) == 0,
    "mcuboot: base address must be 2KB-aligned (Bank0)"
);
_Static_assert(
    (PARAM_PARTITION_MCUBOOT_SIZE % PARAM_FLASH_BANK0_PAGE_SIZE) == 0,
    "mcuboot: size must be a multiple of 2KB (Bank0 page)"
);
_Static_assert(
    (PARAM_PARTITION_MCUBOOT_BASE + PARAM_PARTITION_MCUBOOT_SIZE) <= PARAM_FLASH_BANK0_END_ADDR,
    "mcuboot: must NOT cross Bank0/Bank1 boundary"
);

/* --- mcu_secondary（Bootloader管理，2KB对齐即可） --- */
_Static_assert(
    (PARAM_PARTITION_MCU_SEC_BASE % PARAM_FLASH_BANK0_PAGE_SIZE) == 0,
    "mcu_secondary: base address must be 2KB-aligned"
);
_Static_assert(
    (PARAM_PARTITION_MCU_SEC_SIZE % PARAM_FLASH_BANK0_PAGE_SIZE) == 0,
    "mcu_secondary: size must be a multiple of 2KB"
);

/* --- factory_storage（Bank1, 4KB页） --- */
_Static_assert(
    (PARAM_PARTITION_FACTORY_BASE % PARAM_FLASH_BANK1_PAGE_SIZE) == 0,
    "factory_storage: base address must be 4KB-aligned (Bank1)"
);
_Static_assert(
    (PARAM_PARTITION_FACTORY_SIZE % PARAM_FLASH_BANK1_PAGE_SIZE) == 0,
    "factory_storage: size must be a multiple of 4KB (Bank1 page)"
);
_Static_assert(
    PARAM_PARTITION_FACTORY_BASE >= PARAM_FLASH_BANK0_END_ADDR,
    "factory_storage: must be entirely in Bank1"
);

/* --- bootconf（Bank1, 4KB页） --- */
_Static_assert(
    (PARAM_PARTITION_BOOTCONF_BASE % PARAM_FLASH_BANK1_PAGE_SIZE) == 0,
    "bootconf: base address must be 4KB-aligned (Bank1)"
);
_Static_assert(
    (PARAM_PARTITION_BOOTCONF_SIZE % PARAM_FLASH_BANK1_PAGE_SIZE) == 0,
    "bootconf: size must be a multiple of 4KB (Bank1 page)"
);
_Static_assert(
    PARAM_PARTITION_BOOTCONF_BASE >= PARAM_FLASH_BANK0_END_ADDR,
    "bootconf: must be entirely in Bank1"
);

/* --- app（跨Bank分区，不做跨Bank校验） --- */
/* app分区跨越Bank0/Bank1边界，由Bootloader单独处理擦写。
 *    仅校验起始地址4KB对齐（兼顾两Bank最小页大小）和大小为4KB整数倍。 */
_Static_assert(
    (PARAM_PARTITION_APP_BASE % PARAM_FLASH_BANK1_PAGE_SIZE) == 0,
    "app: base address must be 4KB-aligned"
);
_Static_assert(
    (PARAM_PARTITION_APP_SIZE % PARAM_FLASH_BANK1_PAGE_SIZE) == 0,
    "app: size must be a multiple of 4KB"
);

/* FLASH操作状态 */
#define PARAM_FLASH_OK              0
#define PARAM_FLASH_ERR_ADDR        -1              /**< 地址错误 */
#define PARAM_FLASH_ERR_SIZE        -2              /**< 大小错误 */
#define PARAM_FLASH_ERR_LOCK        -3              /**< 锁定错误 */
#define PARAM_FLASH_ERR_PROGRAM     -4              /**< 编程错误 */
#define PARAM_FLASH_ERR_ERASE       -5              /**< 擦除错误 */

/*******************************************************************************
 * 公开API
 ******************************************************************************/

/*******************************************************************************
 * @brief   初始化FLASH驱动
 * @return  PARAM_FLASH_OK: 成功，其他: 失败
 * @note    初始化FMC控制器，检查分区有效性
 *******************************************************************************/
int param_flash_init(void);

/*******************************************************************************
 * @brief   反初始化FLASH驱动
 * @return  PARAM_FLASH_OK: 成功，其他: 失败
 *******************************************************************************/
int param_flash_deinit(void);

/*******************************************************************************
 * @brief   读取FLASH数据
 * @param   addr    绝对地址（必须在某个可写分区内）
 * @param   buf     输出缓冲区
 * @param   size    读取字节数
 * @return  PARAM_FLASH_OK: 成功，其他: 失败
 * @note    直接内存拷贝，无需FMC解锁，支持所有可写分区
 *******************************************************************************/
int param_flash_read(uint32_t addr, void *buf, uint32_t size);

/*******************************************************************************
 * @brief   编程FLASH数据（按字写入）
 * @param   addr    绝对地址（必须在某个可写分区内）
 * @param   buf     数据缓冲区
 * @param   size    写入字节数（必须是4的倍数）
 * @return  PARAM_FLASH_OK: 成功，其他: 失败
 * @note    写入前必须先擦除，addr必须4字节对齐，支持所有可写分区
 *******************************************************************************/
int param_flash_program(uint32_t addr, const void *buf, uint32_t size);

/*******************************************************************************
 * @brief   擦除FLASH扇区
 * @param   addr    扇区起始地址（必须在某个可写分区内，且页对齐）
 * @param   size    擦除大小（必须是所在Bank页大小的整数倍）
 * @return  PARAM_FLASH_OK: 成功，其他: 失败
 * @note    支持所有可写分区，Bank0按2KB/Bank1按4KB自动识别页大小
 *******************************************************************************/
int param_flash_erase(uint32_t addr, uint32_t size);

/*******************************************************************************
 * @brief   检查地址范围是否在任意可写分区内
 * @param   addr    起始地址
 * @param   size    数据大小
 * @return  true: 合法，false: 非法
 * @note    支持setting/mcu_secondary/factory/bootconf，禁止跨越分区边界
 *******************************************************************************/
bool param_flash_check_addr(uint32_t addr, uint32_t size);

#endif /* __PARAM_FLASH_H__ */
