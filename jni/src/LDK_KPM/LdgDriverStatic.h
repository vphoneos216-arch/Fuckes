#ifndef LDG_KPM_H
#define LDG_KPM_H

#include <cstdint>
#include <cstddef>
#include <mutex>
#include <string>
#include <sys/types.h>

// LdgKpmDriver 驱动操作类
class LdgKpmDriver {
private:
    // 私有实现数据
    class PrivateData;
    PrivateData* pData;

public:
    /**
     * @brief 构造函数，初始化绿豆糕KPM驱动环境
     */
    LdgKpmDriver();

    /**
     * @brief 析构函数，释放KPM相关资源
     */
    ~LdgKpmDriver();

    /**
     * @brief 泛型内存读取模板函数
     * @tparam T 要读取的数据类型（基础类型/结构体均可）
     * @param addr 待读取的内存起始虚拟地址
     * @return 返回读取到的 T 类型数据
     */
    template<typename T>
    T read_legacy(uintptr_t addr) {
        T value{};
        read_legacy(addr, &value, sizeof(T));
        return value;
    }

    /**
     * @brief 内核态内存读取接口
     * @param addr 待读取的内存起始虚拟地址
     * @param buffer 接收读取数据的缓冲区指针
     * @param size 需要读取的字节数量
     * @return 读取成功返回 true，失败返回 false
     */
    bool read_legacy(uintptr_t addr, void* buffer, size_t size);

    /**
     * @brief 用户态通过包名获取进程PID
     * @param package_name 应用包名字符串
     * @return 成功返回对应PID，失败返回0或非法值
     */
    pid_t get_pid_user(const char* package_name);

    /**
     * @brief 内核态通过包名获取进程PID
     * @note 不支持 6.x 及以上内核版本
     * @param package_name 应用包名字符串
     * @return 成功返回对应PID，失败返回0或非法值
     */
    pid_t get_pid_kernel(const char* package_name);

    /**
     * @brief 获取当前进程指定模块基地址
     * @note 此函数为重载函数 可重复命名
     * @param module_name 模块名称字符串
     * @return 模块基址，失败返回0
     */
    uint64_t get_module_base(const char* module_name);

    /**
     * @brief 获取指定进程指定模块基地址
     * @note 此函数为重载函数 可重复命名
     * @param pid 目标进程PID
     * @param module_name 模块名称字符串
     * @return 模块基址，失败返回0
     */
    uint64_t get_module_base(pid_t pid, const char* module_name);

    /**
     * @brief PID初始化绑定
     * @param pid 需要绑定的目标进程PID
     * @return 初始化成功返回true，失败返回false
     */
    bool initialize(pid_t pid);
    
    /**
     * @brief 陀螺仪传感器绑定函数
     * @note 获取可用的陀螺仪传感器节点，随后绑定用于控制陀螺仪
     * @return 绑定成功返回true，失败返回false
     */
    bool gyro_init();

    /**
     * @brief 陀螺仪数据注入
     * @param x 陀螺仪X轴数值
     * @param y 陀螺仪Y轴数值
     * @return 注入成功返回true，失败返回false
     */
    bool gyro_inject(float x, float y);

    /**
     * @brief 停止陀螺仪注入
     * @return 停止成功返回true，失败返回false
     */
    bool gyro_stop();
    
    /**
     * @brief 陀螺仪数据注入 - 旧方案
     * @param x 陀螺仪X轴数值
     * @param y 陀螺仪Y轴数值
     * @return 注入成功返回true，失败返回false
     */
    bool gyro_inject_old(float x, float y);

    /**
     * @brief 停止陀螺仪注入 - 旧方案用
     * @return 停止成功返回true，失败返回false
     */
    bool gyro_stop_old();
};

#endif
