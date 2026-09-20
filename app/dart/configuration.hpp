#pragma once

#include "delay.hpp"
#include "messages.hpp"

namespace dart
{

struct dart_param
{
    int id;
    float yaw_offset;
    float tension_kg_base;
    float tension_kg_outpost;
};

/**
 * @brief 用于打表的结构体，包含了每个dart在不同距离下的yaw_offset和tension_kg
 * 通过插值的方式可以得到更准确的aim参数
 */
struct table_point
{
    int id;
    float distance;
    float yaw_offset;
    float tension_kg;
};

struct aim_point
{
    float yaw_offset;
    float tension_kg;
};

/**
 * @brief 飞镖系统中需要用到的referee信息
 */
struct referee_state
{
    uint32_t game_status;
    uint8_t shooting_remaining_time;
    uint8_t chosen_target;
    uint8_t launch_station_status;
    uint8_t last_launch_station_status;
};

struct auto_aim_state
{
    bool enable;            //<是否允许进入自动模式（主要用于处理赛场遥控器可能离线或手动干预的情况）
    bool yaw_ok;            //<yaw是否已经调整到位
    bool light_lost;        //<视觉绿灯丢失
    bool running;           //<是否正在自动模式
    bool autoaim_allow;     //<是否允许进入准备状态
    bool last_autoaim_allow;//<上一次是否允许进入准备状态的
    bool referee_launch_closed_stable;  //<裁判系统发射站状态稳定在关闭的标志
    bool referee_launch_open_stable;    //<裁判系统发射站状态稳定在打开的标志
    bool vision_door_open_stable;       //<视觉系统门状态稳定在打开的标志
    bool vision_door_closed_stable;     //<视觉系统门状态稳定在关闭的标志
    delay door_open_delay{};
    delay door_closed_delay{};
    delay referee_launch_closed_delay{};
    delay referee_launch_open_delay{};
    delay vision_door_open_delay{};
    delay vision_door_closed_delay{};

};
enum class door_state
{
    // DOOR_OPENING,
    open,
    // DOOR_CLOSING,
    closed,
    // DOOR_UNKNOWN
};

struct dart_config
{
    uint16_t config_revision;
    dart_param dart[17]; //飞镖id从1-16，0号位不使用

    table_point base_distance_table[128]; //打表数据，最多128条
    uint16_t base_distance_table_len;

    int sequence[4]; //发射顺序，长度为4，值为1-16的dart id

    float pre_tension_kg;
};

/**
 * @brief 用于储存飞镖系统运行时状态的结构体
 * 
 */
struct dart_runtime
{
    struct aim_target
    {
        float yaw_offset;
        float tension_kg;
    };

    int current_shot_number;        //<当前是第几发，范围1-4
    int current_dart_id;            //<当前正在发射的飞镖id
    aim_target current_aim_target;   //<当前飞镖解析出的瞄准参数

    door_state vision_door_status;        //<当前视觉判断门的状态
    door_state last_vision_door_status;   //<上一次视觉判断的门状态

    bool last_fire_finished;        //<上一次发射是否完成
    int fired_count_this_open;      //<当前门打开时已发射的飞镖数量

    uint8_t game_status_stable;     //<经过处理后的比赛状态，主要是为了处理裁判系统状态不稳定的情况
    uint8_t game_status_ladar;      //<26赛季为了处理裁判系统不稳定,解析雷达的数据来判断比赛状态，目前没有使用
    char game_status_char;          //<像视觉发送的字符串，用于开始比赛的标志，防止uint8_t类型gamestatus不稳定

    auto_aim_state auto_aim;
    referee_state referee;
};

class dart_state
{
public:
    static constexpr uint16_t BASE_DISTANCE_TABLE_MAX = 128;
    dart_config config;  //<飞镖系统的配置参数
    dart_runtime runtime;//<飞镖系统的运行时状态

    dart_state();

    void update_door(vision_rx* rx);

    void update_prepare();

    void set_table(const table_point* table, uint16_t table_len);

    aim_point lookup_aim(int id, float distance) const;


    slot prepare_slot() const;

    void update_dart_id();
    void update_shot(launcher_status* msg);
    void update_fired(launcher_status* msg);
    void update_history(launcher_status* msg);
};

extern dart_state state;
} // namespace dart
