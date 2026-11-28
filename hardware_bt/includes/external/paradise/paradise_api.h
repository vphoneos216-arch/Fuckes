#ifndef PARADISE_API_H
#define PARADISE_API_H

#include <stdint.h>
#include <sys/types.h>
#include <stddef.h>

/*
 * Keep the friendly C++ API in source while emitting opaque ELF symbol names.
 * Consumers only need to rebuild against this header; no special compiler pass
 * is required for the ABI-name layer.  Define PARADISE_DISABLE_OPAQUE_ABI only
 * for local debugging or when linking an old, pre-protection archive.
 */
#if !defined(PARADISE_DISABLE_OPAQUE_ABI) && \
    (defined(__clang__) || defined(__GNUC__))
#define PARADISE_API_BINARY_NAME(name) __asm__(name)
#else
#define PARADISE_API_BINARY_NAME(name)
#endif

#define PARADISE_GYRO_MASK_GYRO (1u << 0)
#define PARADISE_GYRO_MASK_UNCAL (1u << 1)
#define PARADISE_GYRO_MASK_ALL (PARADISE_GYRO_MASK_GYRO | PARADISE_GYRO_MASK_UNCAL)


#define PARADISE_BP_OP_NONE    0x0
#define PARADISE_BP_OP_READ    0x1
#define PARADISE_BP_OP_WRITE   0x2
#define PARADISE_BP_MAX_POINTS 16
#define PARADISE_BP_MAX_RECORDS 256

/* Source-compatible capacity aliases for existing callers. */
#define PARADISE_HWBP_MAX_POINTS PARADISE_BP_MAX_POINTS
#define PARADISE_HWBP_MAX_RECORDS PARADISE_BP_MAX_RECORDS
#define PARADISE_PTEBP_MAX_POINTS PARADISE_BP_MAX_POINTS
#define PARADISE_PTEBP_MAX_RECORDS PARADISE_BP_MAX_RECORDS
#define PARADISE_CLONEBP_MAX_POINTS PARADISE_BP_MAX_POINTS
#define PARADISE_CLONEBP_MAX_RECORDS PARADISE_BP_MAX_RECORDS

/* One breakpoint vocabulary is shared by HWBP, PTEBP, and clonebp. */
enum paradise_bp_type {
    PARADISE_BP_EMPTY = 0,
    PARADISE_BP_READ = 1,
    PARADISE_BP_WRITE = 2,
    PARADISE_BP_READ_WRITE = 3,
    PARADISE_BP_EXECUTE = 4,

    PARADISE_HWBP_EMPTY = PARADISE_BP_EMPTY,
    PARADISE_HWBP_READ = PARADISE_BP_READ,
    PARADISE_HWBP_WRITE = PARADISE_BP_WRITE,
    PARADISE_HWBP_READ_WRITE = PARADISE_BP_READ_WRITE,
    PARADISE_HWBP_EXECUTE = PARADISE_BP_EXECUTE,
};

using paradise_hwbp_type = paradise_bp_type;

enum paradise_bp_scope {
    PARADISE_BP_MAIN_THREAD = 0,
    PARADISE_BP_OTHER_THREADS = 1,
    PARADISE_BP_ALL_THREADS = 2,

    PARADISE_HWBP_MAIN_THREAD = PARADISE_BP_MAIN_THREAD,
    PARADISE_HWBP_OTHER_THREADS = PARADISE_BP_OTHER_THREADS,
    PARADISE_HWBP_ALL_THREADS = PARADISE_BP_ALL_THREADS,
};

using paradise_hwbp_scope = paradise_bp_scope;

enum paradise_bp_register {
    PARADISE_REG_PC = 0,
    PARADISE_REG_HIT_COUNT,
    PARADISE_REG_LR,
    PARADISE_REG_SP,
    PARADISE_REG_ORIG_X0,
    PARADISE_REG_SYSCALLNO,
    PARADISE_REG_PSTATE,
    PARADISE_REG_X0,
    PARADISE_REG_X1,
    PARADISE_REG_X2,
    PARADISE_REG_X3,
    PARADISE_REG_X4,
    PARADISE_REG_X5,
    PARADISE_REG_X6,
    PARADISE_REG_X7,
    PARADISE_REG_X8,
    PARADISE_REG_X9,
    PARADISE_REG_X10,
    PARADISE_REG_X11,
    PARADISE_REG_X12,
    PARADISE_REG_X13,
    PARADISE_REG_X14,
    PARADISE_REG_X15,
    PARADISE_REG_X16,
    PARADISE_REG_X17,
    PARADISE_REG_X18,
    PARADISE_REG_X19,
    PARADISE_REG_X20,
    PARADISE_REG_X21,
    PARADISE_REG_X22,
    PARADISE_REG_X23,
    PARADISE_REG_X24,
    PARADISE_REG_X25,
    PARADISE_REG_X26,
    PARADISE_REG_X27,
    PARADISE_REG_X28,
    PARADISE_REG_X29,
    PARADISE_REG_FPSR,
    PARADISE_REG_FPCR,
    PARADISE_REG_Q0,
    PARADISE_REG_Q1,
    PARADISE_REG_Q2,
    PARADISE_REG_Q3,
    PARADISE_REG_Q4,
    PARADISE_REG_Q5,
    PARADISE_REG_Q6,
    PARADISE_REG_Q7,
    PARADISE_REG_Q8,
    PARADISE_REG_Q9,
    PARADISE_REG_Q10,
    PARADISE_REG_Q11,
    PARADISE_REG_Q12,
    PARADISE_REG_Q13,
    PARADISE_REG_Q14,
    PARADISE_REG_Q15,
    PARADISE_REG_Q16,
    PARADISE_REG_Q17,
    PARADISE_REG_Q18,
    PARADISE_REG_Q19,
    PARADISE_REG_Q20,
    PARADISE_REG_Q21,
    PARADISE_REG_Q22,
    PARADISE_REG_Q23,
    PARADISE_REG_Q24,
    PARADISE_REG_Q25,
    PARADISE_REG_Q26,
    PARADISE_REG_Q27,
    PARADISE_REG_Q28,
    PARADISE_REG_Q29,
    PARADISE_REG_Q30,
    PARADISE_REG_Q31,
    PARADISE_REG_COUNT,
};

using paradise_hwbp_register = paradise_bp_register;

#define PARADISE_BP_SET_MASK(record, reg, op)                       \
    do {                                                            \
        int byte_index = (reg) >> 2;                                \
        int bit_offset = ((reg) & 0x3) << 1;                        \
        (record)->mask[byte_index] &= ~(0x3 << bit_offset);         \
        (record)->mask[byte_index] |= ((op) & 0x3) << bit_offset;   \
    } while (0)

#define PARADISE_BP_GET_MASK(record, reg) \
    (((record)->mask[(reg) >> 2] >> (((reg) & 0x3) << 1)) & 0x3)

struct paradise_bp_record {
    uint8_t mask[18];
    uint64_t hit_count;
    uint64_t pc;
    uint64_t lr;
    uint64_t sp;
    uint64_t orig_x0;
    int32_t syscallno;
    uint32_t syscallno_padding;
    uint64_t pstate;
    uint64_t x0, x1, x2, x3, x4, x5, x6, x7, x8, x9;
    uint64_t x10, x11, x12, x13, x14, x15, x16, x17, x18, x19;
    uint64_t x20, x21, x22, x23, x24, x25, x26, x27, x28, x29;
    uint32_t fpsr;
    uint32_t fpcr;
    __uint128_t q0, q1, q2, q3, q4, q5, q6, q7, q8, q9;
    __uint128_t q10, q11, q12, q13, q14, q15, q16, q17, q18, q19;
    __uint128_t q20, q21, q22, q23, q24, q25, q26, q27, q28, q29;
    __uint128_t q30, q31;
};

struct paradise_bp_point_config {
    uint64_t address;
    uint32_t type;
    uint32_t length;
    uint32_t scope;
    uint32_t reserved;
};

struct paradise_bp_config {
    int32_t tgid;
    uint32_t point_count;
    uint64_t num_brps;
    uint64_t num_wrps;
    paradise_bp_point_config points[PARADISE_BP_MAX_POINTS];
};

/* Legacy names remain source aliases; all three backends now exchange the
 * same canonical types and therefore share one C++ ABI on rebuild. */
using paradise_hwbp_record = paradise_bp_record;
using paradise_ptebp_record = paradise_bp_record;
using paradise_clonebp_record = paradise_bp_record;
using paradise_hwbp_point_config = paradise_bp_point_config;
using paradise_ptebp_point_config = paradise_bp_point_config;
using paradise_clonebp_point_config = paradise_bp_point_config;
using paradise_hwbp_config = paradise_bp_config;
using paradise_ptebp_config = paradise_bp_config;
using paradise_clonebp_config = paradise_bp_config;

#define PARADISE_PTEBP_MAX_COORD_SAMPLES 2048
#define PARADISE_PTEBP_POINT_COORD_X0_Q0_Q1_Q2             (1U << 0)
#define PARADISE_PTEBP_POINT_COORD_X20_XY_X8_Z_X9          (1U << 1)
#define PARADISE_PTEBP_POINT_COORD_X19_XY_X9_Z_X8          (1U << 2)
#define PARADISE_PTEBP_POINT_COORD_X19_Q0_Q1_Q2            (1U << 3)
#define PARADISE_PTEBP_POINT_COORD_X22_XY_X8_Z_X10         (1U << 4)
#define PARADISE_PTEBP_POINT_COORD_MASK                     \
    (PARADISE_PTEBP_POINT_COORD_X0_Q0_Q1_Q2 |              \
     PARADISE_PTEBP_POINT_COORD_X20_XY_X8_Z_X9 |           \
     PARADISE_PTEBP_POINT_COORD_X19_XY_X9_Z_X8 |           \
     PARADISE_PTEBP_POINT_COORD_X19_Q0_Q1_Q2 |             \
     PARADISE_PTEBP_POINT_COORD_X22_XY_X8_Z_X10)

struct paradise_ptebp_coord_sample {
    uint64_t sequence;
    uint64_t hit_count;
    uint64_t pc;
    uint64_t component;
    uint32_t x_bits;
    uint32_t y_bits;
    uint32_t z_bits;
    uint32_t reserved;
};

static_assert(sizeof(paradise_bp_record) == 848,
              "paradise_bp_record ABI mismatch");
static_assert(alignof(paradise_bp_record) == 16,
              "paradise_bp_record alignment mismatch");
static_assert(offsetof(paradise_bp_record, syscallno) == 64 &&
              offsetof(paradise_bp_record, pstate) == 72 &&
              offsetof(paradise_bp_record, q0) == 336,
              "paradise_bp_record field layout mismatch");
static_assert(sizeof(paradise_bp_point_config) == 24,
              "paradise_bp_point_config ABI mismatch");
static_assert(sizeof(paradise_bp_config) == 408,
              "paradise_bp_config ABI mismatch");
static_assert(offsetof(paradise_bp_config, points) == 24,
              "paradise_bp_config field layout mismatch");
static_assert(sizeof(paradise_ptebp_coord_sample) == 48,
              "paradise_ptebp_coord_sample ABI mismatch");
class paradise_driver {
private:
    pid_t pid;
    int fd;
    uint32_t touch_x_proportion = 0;
    uint32_t touch_y_proportion = 0;

    int install_driver_fd() PARADISE_API_BINARY_NAME("p_d9e1fc6b24fab117");
    void ensure_connected() PARADISE_API_BINARY_NAME("p_10c704d4211304c5");

public:
    // 构造时连接 Paradise 驱动
    paradise_driver() PARADISE_API_BINARY_NAME("p_0bb7a995a7fab87d");
    
    // 构析方法
    ~paradise_driver() PARADISE_API_BINARY_NAME("p_25da1fd05b260ffa");

    // 初始化目标 pid，读写前务必调用一次
    void initialize(pid_t target_pid)
        PARADISE_API_BINARY_NAME("p_02c4b0d20c5c5cee");
    
    // 获取进程pid，传入进程名称，从内核层安全获取pid
    pid_t get_pid(const char *name)
        PARADISE_API_BINARY_NAME("p_f71738232233861b");
    
    // 获取模块基址，传入模块名，从内核层安全获取模块基址
    uintptr_t get_module_base(const char *name)
        PARADISE_API_BINARY_NAME("p_ccf66831679d5ba0");
    
    // 更新陀螺仪数据
    bool gyro_update(float x, float y,
                     uint32_t type_mask = PARADISE_GYRO_MASK_ALL,
                     bool enable = true)
        PARADISE_API_BINARY_NAME("p_8b7bd86d6994f813");
    
    // 检查进程是否存活 (alive_out: 1为存活，0为未存活)
    bool is_process_alive(pid_t check_pid, int *alive_out)
        PARADISE_API_BINARY_NAME("p_448af80cb939db97");
    
    // 隐藏或取消隐藏指定进程
    bool hide_process(pid_t target_pid, bool hide)
        PARADISE_API_BINARY_NAME("p_a1138f4bc4f950ab");
    
    // 隐藏或取消隐藏指定路径
    bool hide_path(const char *path, bool hide)
        PARADISE_API_BINARY_NAME("p_eda39d399bc07234");
    
    // 获取进程列表位图
    bool list_processes(uint8_t *bitmap, size_t bitmap_size,
                        size_t *process_count_out)
        PARADISE_API_BINARY_NAME("p_52fe5ca87b63ba68");

    // 获取进程主线程的 TPIDR_EL0。
    uintptr_t get_main_thread_elf0(pid_t target_pid)
        PARADISE_API_BINARY_NAME("p_c707678af87d2a1c");

    // 获取指定线程的 TPIDR_EL0。
    uintptr_t get_thread_tpidr_el0(pid_t tid)
        PARADISE_API_BINARY_NAME("p_a4a7096734a1a796");

    // 获取指定线程的 PACGA 密钥及算法编号。
    bool get_thread_pacga_key(pid_t tid, uint64_t *lo, uint64_t *hi,
                              uint32_t *algo = nullptr)
        PARADISE_API_BINARY_NAME("p_f5e89508bbe4c125");

    // 获取指定线程的 PACIA 密钥及算法编号。
    bool get_thread_pacia_key(pid_t tid, uint64_t *lo, uint64_t *hi,
                              uint32_t *algo = nullptr)
        PARADISE_API_BINARY_NAME("p_f034ee5989a70928");

    // 将 tid 的 PACGA 密钥绑定为 target_tid 的 PACGA 密钥。
    bool bind_thread_pacga_key(pid_t tid, pid_t target_tid)
        PARADISE_API_BINARY_NAME("p_038a79f09709a350");
    
    // 读取，传入地址、接收指针、类型大小
    bool read(uintptr_t addr, void *buffer, size_t size)
        PARADISE_API_BINARY_NAME("p_668648ae6071fb0a");
    
    // 写入，传入地址、数据指针、类型大小
    bool write(uintptr_t addr, void *buffer, size_t size)
        PARADISE_API_BINARY_NAME("p_53cce3880e2ac65b");

    // 输入屏幕宽高，内部按短边 X、长边 Y 保存旧接口的整数缩放比例。
    bool touch_init(int abs_screen_x, int abs_screen_y)
        PARADISE_API_BINARY_NAME("p_b1f3384c6f6878d2");

    // x/y 沿用原有屏幕坐标，内部转换为 ABS_MT 坐标；slot 为 0..9。
    // 重复 down 不移动；移动使用 touch_move。
    bool touch_down(int slot, int x, int y)
        PARADISE_API_BINARY_NAME("p_d6f2709a2216c992");
    bool touch_move(int slot, int x, int y)
        PARADISE_API_BINARY_NAME("p_1e41118ea37f1bb5");

    // 抬起指定 slot
    bool touch_up(int slot)
        PARADISE_API_BINARY_NAME("p_3ffdaf808ba67e87");

    // 原始 ABS_MT 坐标接口，不做缩放：command=2/3/4，mask 低十位；零掩码默认槽位 9。
    bool touch_event(uint32_t command, uint32_t x, uint32_t y,
                     uint16_t mask)
        PARADISE_API_BINARY_NAME("p_3d3dc0ed474110d9");

    // 设置最多 16 个 ARM64 执行断点/访问观察点。
    bool hwbp_set(const paradise_bp_point_config *points, size_t point_count)
        PARADISE_API_BINARY_NAME("p_7c34596de783111a");

    // 删除当前硬件断点/观察点并清理所有 CPU 调试寄存器。
    bool hwbp_remove()
        PARADISE_API_BINARY_NAME("p_4a288cc36e5aae6e");

    // 获取当前配置以及 CPU 支持的 BRP/WRP 数量。
    bool hwbp_get_config(paradise_bp_config *config_out)
        PARADISE_API_BINARY_NAME("p_c58599795c857381");

    // 分段读取指定点位按触发 PC 聚合的现场记录。
    bool hwbp_get_records(uint32_t point_index, uint32_t start_index,
                          paradise_bp_record *records, uint32_t capacity,
                          uint32_t *count_out)
        PARADISE_API_BINARY_NAME("p_b81c434d45b91242");

    // 写回一条记录及其 2-bit 寄存器操作掩码。
    bool hwbp_set_record(uint32_t point_index, uint32_t record_index,
                         const paradise_bp_record *record)
        PARADISE_API_BINARY_NAME("p_36d69b7ad841f608");

    // 清空一个点位的记录；point_index=-1 清空全部点位。
    bool hwbp_clear_records(int32_t point_index = -1)
        PARADISE_API_BINARY_NAME("p_e38de88cedf09b45");

    // PTE 执行断点：最多 16 个，type=EXECUTE、length=4。
    bool ptebp_set(const paradise_bp_point_config *points, size_t point_count)
        PARADISE_API_BINARY_NAME("p_2c5eb42a308f8d47");
    bool ptebp_remove()
        PARADISE_API_BINARY_NAME("p_d9c0aecb3e8c1144");

    // num_brps/num_wrps 为零；监控自动撤销后返回空配置，记录仍可读取。
    bool ptebp_get_config(paradise_bp_config *config_out)
        PARADISE_API_BINARY_NAME("p_38c015a49bf6d0d3");
    bool ptebp_get_records(uint32_t point_index, uint32_t start_index,
                           paradise_bp_record *records, uint32_t capacity,
                           uint32_t *count_out)
        PARADISE_API_BINARY_NAME("p_ad5d834e2b0c8768");
    bool ptebp_set_record(uint32_t point_index, uint32_t record_index,
                          const paradise_bp_record *record)
        PARADISE_API_BINARY_NAME("p_6c8663ed8676dbe0");
    bool ptebp_clear_records(int32_t point_index = -1)
        PARADISE_API_BINARY_NAME("p_129c73108eb0edc2");
    bool ptebp_get_coord_samples(uint64_t after_sequence,
                                 paradise_ptebp_coord_sample *samples,
                                 uint32_t capacity, uint32_t *count_out,
                                 uint64_t *newest_sequence_out)
        PARADISE_API_BINARY_NAME("p_9612a176e4f4dc42");

    // 克隆页断点：对 initialize() selector 所属 TGID 的共享 mm
    // 安装 shadow page。scope 相对 selector 解释：MAIN=仅 selector，
    // OTHER=同 TGID 内除 selector 外的线程，ALL=同 TGID 全部线程。
    bool clonebp_set(const paradise_bp_point_config *points,
                     size_t point_count)
        PARADISE_API_BINARY_NAME("p_033422eedb588a05");
    bool clonebp_remove()
        PARADISE_API_BINARY_NAME("p_c4a54a36b4d3265d");
    bool clonebp_get_config(paradise_bp_config *config_out)
        PARADISE_API_BINARY_NAME("p_d1438ddaec436aaa");
    bool clonebp_get_records(uint32_t point_index, uint32_t start_index,
                             paradise_bp_record *records,
                             uint32_t capacity, uint32_t *count_out)
        PARADISE_API_BINARY_NAME("p_ebdffe3e85919097");
    bool clonebp_set_record(uint32_t point_index, uint32_t record_index,
                            const paradise_bp_record *record)
        PARADISE_API_BINARY_NAME("p_45f3f694fd7758ff");
    bool clonebp_clear_records(int32_t point_index = -1)
        PARADISE_API_BINARY_NAME("p_cb0d915d88ed87ae");

    // 模板方法，传入地址，返回地址上的值
    template <typename T>
    T read(uintptr_t addr)
    {
        T res{};
        if (this->read(addr, &res, sizeof(T)))
            return res;
        return {};
    }

    // 模板方法，传入地址，修改后的值
    template <typename T>
    bool write(uintptr_t addr, T value)
    {
        return this->write(addr, &value, sizeof(T));
    }
};

#undef PARADISE_API_BINARY_NAME

#endif
