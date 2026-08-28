#pragma once

#include <stdint.h>
#include "HostProtocol.hpp"
#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    Down = 2, //Relax
    Mid = 3,  //Normal
    Up = 1,   //Spin
    D2M = 4,  //R2N
    M2D = 5,  //N2R
    M2U = 6,  //N2S
    U2M = 7   //S2N
}CTRL_STATE;

// typedef enum {
//     Closed = 2,
//     Warm = 3,
//     Fire = 1
// }SHOOT_STATE;

typedef enum {
    SPD,
    POS,
    TORQUE
}CTRL_MODE;

/**
 * @brief 遥控器消息结构
 */
struct msg_remoter_t 
{
    CTRL_STATE left_sw;
    CTRL_STATE right_sw;
    CTRL_STATE last_left_sw;
    CTRL_STATE last_right_sw;
    float left_x;
    float left_y;
    float right_x;
    float right_y;
    float mouse_x;
    float mouse_y;
    float mouse_z;
    bool mouse_left;
    bool mouse_right;

    struct __attribute__((packed)) {
        uint16_t W : 1;
        uint16_t S : 1;
        uint16_t A : 1;
        uint16_t D : 1;
        uint16_t SHIFT : 1;
        uint16_t CTRL : 1;
        uint16_t Q : 1;
        uint16_t E : 1;
        uint16_t R : 1;
        uint16_t F : 1;
        uint16_t G : 1;
        uint16_t Z : 1;
        uint16_t X : 1;
        uint16_t C : 1;
        uint16_t V : 1;
        uint16_t B : 1;
    } key;

    struct __attribute__((packed)) {
       uint16_t W : 1;
       uint16_t S : 1;
       uint16_t A : 1;
       uint16_t D : 1;
       uint16_t SHIFT : 1;
       uint16_t CTRL : 1;
       uint16_t Q : 1;
       uint16_t E : 1;
       uint16_t R : 1;
       uint16_t F : 1;
       uint16_t G : 1;
       uint16_t Z : 1;
       uint16_t X : 1;
       uint16_t C : 1;
       uint16_t V : 1;
       uint16_t B : 1;
    } last_key;

    bool offline;
};

/**
 * @brief AHRS消息结构
 */
struct msg_ins_t {
    float quaternion[4];    ///< 四元数
    float roll;             ///< 横滚角, deg
    float pitch;            ///< 俯仰角, deg
    float yaw;              ///< 偏航角, deg
    float total_yaw;        ///< 偏航总角度, deg
    float gyro_r;           ///< roll角速度, rad/s
    float gyro_p;           ///< pitch角速度, rad/s
    float gyro_y;           ///< yaw角速度, rad/s
    float accel[3];
};


struct msg_visionrx_t
{
    uint8_t header; // 发送数据包的头
    // float distance_reserve;
    // float angle_reserve;
    // uint8_t flag_reserve;
    uint8_t light_detected; //0: unknown/no target /盲区内
                            //1: green light visible and aim data valid 
                            //2: door open but green light occluded //绿灯被遮
                            //3: door not fully open/blocked
    uint8_t stable_state;//0不稳定，1稳定
    float yaw; //! 目前下位机用的是旧版，rm26是相机中心和绿灯的像素点差，新版准备用绝对yaw_rad
    float distance;
    uint16_t checksum; // 校验和
    
}__attribute__((packed));


struct msg_visiontx_t
{
    uint8_t header; //0x5A
    uint8_t target_id; //0-outpost 1-base
    uint8_t DartNumber;//1,2,3,4
    float offset;
    // uint8_t start_state;
    // char  start_state_char;
    float yaw; //当前和标定的正中间零点的yaw值偏差

    // uint8_t selected_target_id;


    uint16_t checksum;
} __attribute__((packed));

struct logger_t
{
    uint8_t header;
    uint8_t state;
    uint8_t prepare_state;
    uint8_t launch_station_status;
    bool is_fire_finished;
    uint8_t fired_count_this_open;
    uint8_t current_shot_number;
    uint8_t current_dart_id;
    uint8_t door_status;
    uint8_t last_light_detected;
    uint8_t vision_light_detected;
    uint8_t vision_stable_state;
    bool door_session_active;
    bool autoaim_allow;
    bool door_close_inhibit_active;
    float string_L_force_kg;
    float string_R_force_kg;
    uint16_t checksum;
} __attribute__((packed));

/**
 * @brief 飞镖发射指令
 */
typedef enum
{
    DART_RELAX = 0,     // 放松或急停
    DART_PREPARE= 1,            //调整yaw角度，
    DART_SYN_ADJUST = 2,     //调整同步带
    DART_STRING_ADJUST = 3,  //调整副弦
    DART_FIRE = 4,           // 发射
    DART_TRIGGER_OPEN,
    DART_TRIGGER_CLOSE,
    DART_YAW_ADJUST,
    DART_PRE_TENSION
} LAUNCHER_ACTION;

/**
 * @brief Dart slot index
 * slot 1 is the preloaded first dart, slot 2~4 are the reload positions
 */
typedef enum
{
    DART_SLOT_NONE = 0,
    DART_SLOT_1 = 1,
    DART_SLOT_2 = 2,
    DART_SLOT_3 = 3,
} DART_SLOT;

/**
 * @brief 飞镖cmd，由TaskSysctrl发送给TaskLauncher
 */
struct msg_cmd_t
{
    LAUNCHER_ACTION action;
    DART_SLOT next_dart_slot;
    uint8_t current_shot_number;

    float yaw;              //约定为-1~1
    float tension_kg;
    float pre_tension_kg;
    float rc_syn;
    float rc_string_L;
    float rc_string_R;
};

typedef enum TriggerStatus
{
    open_then_relax = 0,
    lock_then_relax,
    open_and_remain,
    lock_and_remain,
} TriggerStatus;

/**
 * @brief 电机控制消息结构，由Tasklauncher发送给Taskmotors
 * yaw轴步进电机
 */
struct msg_motor_ctrl_t
{
    float yaw_spd;
    float yaw_tq;
    float yaw_pos;
    CTRL_MODE yaw_mode;

    // bool trigger_lock;
    // TriggerStatus trigger;
    bool trigger_release;

    CTRL_MODE Coil_L_mode;
    CTRL_MODE Coil_R_mode;

    CTRL_MODE Coil_mode;

    CTRL_MODE string_L_mode;

    float Coil_L_spd;
    float Coil_R_spd;



    float Coil_L_pos;
    float Coil_R_pos;

    float Coil_back_pos;
    float Coil_forward_pos;

    float synbelt_spd;
    float synbelt_pos;
    CTRL_MODE synbelt_mode;

    float string_L_spd;
    float string_R_spd;

    float string_L_tension_kg;
    float string_R_tension_kg;

    bool string_able;

    DART_SLOT gantry_target_slot;
};

struct msg_motorfdb_t
{
    float Lcoil_pos_fdb;
    float Rcoil_pos_fbd;
    float gantry_pos_fdb;
    float gantry_spd_fdb;
    float gantry_pos_set;

    float syn_pos_fdb;
    float syn_tq_fdb;

    float yaw_pos_fdb;
};

// struct tof_data_t
// {
//     uint8_t header[2];
//     uint16_t distance;
//     uint16_t strength;
//     uint16_t temp_raw;
//     uint8_t check_sum;
// };

/**
 * @brief 储存各传感器发送的标志位
 */
struct msg_sensor_t
{
    bool is_coil_L_reset;
    bool is_coil_R_reset;
    bool is_coil_reset;         //卷簧是否归位(上方)
    bool is_door_open;          //舱门是否打开
    bool is_string_tight;       //暂保留，后续可能删除
    bool is_launchplat_return;  //发射台是否归位
    bool is_fire_done;          //是否发射完成，可能不需要
    bool is_dart_loaded;        //飞镖装填完毕
    float string_L_force_kg;    //左副弦拉力，单位kg
    float string_R_force_kg;    //右副弦拉力，单位kg
    bool is_trigger_locked;     //扳机是否锁定
};

struct debug_motor_t
{
    float speed;
    float position;
    float current;
    float torque;
};


struct msg_launcher2sysctrl_t
{
    uint8_t current_state; //保留
    bool is_fire_finished; //todo:发射是否完成,由launcher逻辑判断？
    bool last_fire_finished; //用于边缘检测
};

#ifdef __cplusplus
}

#include "config_referee.hpp"

struct msg_referee_t
{
    GameStatus_t GameStatus;
    DartInfo_t DartInfo;
    DartClientCmd_t DartClientCmd;
    RoboInteractData_t RoboInteractData;
};

// Radar custom 0x0301 payload, data_cmd_id must be 0x0201.
struct OutpostStatusMsg
{
    uint16_t data_cmd_id;   // 0x0201
    uint16_t sender_id;     // Blue radar 109 / Red radar 9
    uint16_t receiver_id;   // Blue sentry 107, dart 108 / Red sentry 7, dart 8
    uint8_t event;
} __attribute__((packed));

#endif
