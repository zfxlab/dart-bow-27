/**
 * @file TaskSysCtrl.cpp
 * @author Aeskyrr17
 * @brief  处理手控遥控器逻辑与自动模式逻辑，发布cmd_topic
 *
 */
#include "main.h"
#include "tx_api.h"
#include <string.h>

#include "om.h"
#include "magicmsgs.hpp"
#include "DartHostService.hpp"
#include "TaskSysCtrl.hpp"

TX_THREAD SysctrlThread;
uint8_t SysctrlThreadStack[2048] = {0};
TX_SEMAPHORE VisionErrorSem;

void Run_Auto_Control(const msg_visionrx_t* rx,DartLibrary* dart, msg_cmd_t* cmd);
void Update_referee_data(msg_referee_t* rawdata, DartLibrary* dart);

DartLibrary dart_lib;
DartHostService dart_host_service;

DartRuntime::AimTarget Resolve_Current_Aim_Target(const DartLibrary& dart, float vision_distance);
void Init_Dart_Config(DartLibrary* dart);
void Build_Remoter_Command(const msg_remoter_t& remoter,float tension_kg,msg_cmd_t* cmd);
void Resolve_Final_Command(bool autoAim_control,const msg_remoter_t& remoter,const msg_visionrx_t& vision_rx,
                            DartLibrary* dart,msg_cmd_t* cmd);

#define FORCE_TABLING
//! 测试的时候用dart_lib里面的固定值
const bool auto_aim_on_power_up = false;
//! true: 直接run autoAim直到遥控器干预。
//! false: 先用遥控器remoter Up/Up来开启autoAim并latch

/**
 * @brief 初始化飞镖配置
 * @param dart DartLibrary 实例
 */
void Init_Dart_Config(DartLibrary* dart)
{
    DartConfig& config = dart->config;

    config.dart[1] = {1, -0.40f, 64.5f, 72.0f};
    config.dart[2] = {2, -0.40f, 64.5f, 72.0f};
    config.dart[3] = {3, -0.08f, 70.0f, 72.0f};
    config.dart[4] = {4, -0.08f, 68.75f, 72.0f};

    config.dart[5] = {5, -0.10f, 68.5f, 50.0f};
    config.dart[6] = {6, -0.20f, 65.5f, 50.0f};
    config.dart[7] = {7, -0.18f, 69.0f, 50.0f};
    config.dart[8] = {8, -0.08f, 68.4f, 50.0f};

    config.dart[9]  = {9,  0.00f, 69.0f, 50.0f};
    config.dart[10] = {10, 0.00f, 69.0f, 50.0f};
    config.dart[11] = {11, 0.00f, 69.0f, 50.0f};
    config.dart[12] = {12, 0.00f, 69.0f, 50.0f};

    config.dart[13] = {13, 0.00f, 69.0f, 50.0f};
    config.dart[14] = {14, 0.00f, 69.0f, 50.0f};
    config.dart[15] = {15, 0.00f, 69.0f, 50.0f};
    config.dart[16] = {16, 0.00f, 69.0f, 50.0f};


    const Dart_Base_Table_Point_t base_distance_table[] = {
        {1,  25.0f, -0.80f, 66.0f},
        {2,  25.0f, -1.20f, 67.0f},
        {3,  25.0f, -0.68f, 66.0f},
        {4,  25.0f, -1.20f, 67.0f},
        {5,  25.0f, -0.90f, 64.5f},
        {6,  25.0f, -0.75f, 65.5f},
        {7,  25.0f,  0.00f, 82.0f},
        {8,  25.0f, -0.70f, 66.0f},
        {9,  25.0f,  0.00f, 69.0f},
        {10, 25.0f,  0.00f, 69.0f},
        {11, 25.0f,  0.00f, 69.0f},
        {12, 25.0f,  0.00f, 69.0f},
        {13, 25.0f,  0.00f, 69.0f},
        {14, 25.0f,  0.00f, 69.0f},
        {15, 25.0f,  0.00f, 69.0f},
        {16, 25.0f,  0.00f, 69.0f},
    };

    dart->Set_Base_Distance_Table(
        base_distance_table,
        static_cast<uint16_t> 
        (sizeof(base_distance_table) / sizeof(base_distance_table[0]))
    );

    // sequence 的下标表示第几发；
    // sequence 的值表示实际使用的物理飞镖编号。
    config.sequence[0] = 3;
    config.sequence[1] = 4;
    config.sequence[2] = 5;
    config.sequence[3] = 8;

    config.pre_tension_kg = 32.0f;
}

    msg_remoter_t remoter{};

[[nonreturn]] void SysctrlThreadFun(ULONG initial_input)
{
    UNUSED(initial_input);

    //处理onemessage数据
    om_topic_t *cmd_topic = om_config_topic(nullptr, "ca", "cmd", sizeof(msg_cmd_t));
    msg_cmd_t cmd{};

    om_topic_t *visiontx_topic = om_config_topic(nullptr, "ca", "visiontx", sizeof(msg_visiontx_t));
    msg_visiontx_t vision_tx{};

    om_suber_t *remoter_suber = om_subscribe(om_find_topic("remoter", UINT32_MAX));
    // msg_remoter_t remoter{};
    om_suber_t *lch2sys_suber = om_subscribe(om_find_topic("lch2sys",UINT32_MAX));
    msg_launcher2sysctrl_t lch2sys{};
    om_suber_t *referee_suber = om_subscribe(om_find_topic("referee", UINT32_MAX));
    msg_referee_t referee_pack{};
    om_suber_t *visionrx_suber = om_subscribe(om_find_topic("visionrx",UINT32_MAX));
    msg_visionrx_t vision_rx{};

    om_suber_t *motorfdb_suber = om_subscribe(om_find_topic("motorfdb", UINT32_MAX));
    msg_motorfdb_t motorfdb{};

    //config initialization
    Init_Dart_Config(&dart_lib);
    dart_host_service.Init();

    for (;;)
    {
        memset(&cmd, 0, sizeof(msg_cmd_t)); //每次循环清空cmd
        om_suber_export(remoter_suber, &remoter, false);
        om_suber_export(lch2sys_suber, &lch2sys, false);
        om_suber_export(visionrx_suber,&vision_rx,false);
        om_suber_export(referee_suber,&referee_pack,false);
        om_suber_export(motorfdb_suber, &motorfdb, false);
        Update_referee_data(&referee_pack,&dart_lib);

        //Update runtime state
        dart_lib.Update_Current_State(&lch2sys);
        dart_lib.UPDATE_DOOR_STATUS(&vision_rx);
        dart_lib.Update_AutoAim_Prepare_Allowed();
        dart_lib.runtime.autoAim.running = false;
        dart_lib.Update_Fired_State(&lch2sys);
        dart_lib.Update_Current_Dart_Id();

        dart_host_service.Poll(dart_lib, lch2sys);

        //遥控器离弦的自动模式锁存逻辑：上电后Up/Up开启autoAim，下电后保持，直到遥控器干预才关闭autoAim
        const bool autoAim_request = (!remoter.offline && remoter.left_sw == Up && remoter.right_sw == Up);
        bool autoAim_control = false;
        if (auto_aim_on_power_up) //直接上电开启autoAim，不需要Up/Up锁存
        {
            const bool remoter_intervention = (!remoter.offline && !autoAim_request);
            dart_lib.runtime.autoAim.enable = !remoter_intervention;
            autoAim_control = dart_lib.runtime.autoAim.enable;
        }
        else
        {
            if (!remoter.offline)
            {
                dart_lib.runtime.autoAim.enable = autoAim_request;
            }
            autoAim_control = dart_lib.runtime.autoAim.enable && (remoter.offline || autoAim_request);
        }

        //在这里进行一些测试的硬编码
        // ! !!!!!！！！！！！！！！！！！！！！！！!测试代码
        //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        // vision_rx.distance = 25.0f;
        // dart_lib.runtime.referee.game_status = 4;
        // dart_lib.runtime.door_status = DOOR_OPEN;
        dart_lib.runtime.referee.chosen_target = 1;
        // dart_lib.runtime.referee.chosen_target = 0; //前哨


        //更新tension_kg和yaw数据
        int id = dart_lib.runtime.current_dart_id;
        dart_lib.runtime.current_aim_target = Resolve_Current_Aim_Target(dart_lib, vision_rx.distance);

        cmd.tension_kg = dart_lib.runtime.current_aim_target.tension_kg;
        cmd.next_dart_slot = dart_lib.Get_Prepare_Slot();
        cmd.current_shot_number = dart_lib.runtime.current_shot_number;

        //处理vision_tx数据
        vision_tx.header = 0x5A;
        vision_tx.offset = dart_lib.runtime.current_aim_target.yaw_offset;
        vision_tx.DartNumber = id;
        vision_tx.target_id = dart_lib.runtime.referee.chosen_target;
        vision_tx.yaw = motorfdb.yaw_pos_fdb;
        // if (dart_lib.runtime.referee.game_status == 4 || dart_lib.runtime.game_status_ladar == 1 ||
        //     (dart_lib.runtime.referee.shooting_remaining_time <= 30 && dart_lib.runtime.referee.shooting_remaining_time > 1))
        // {
        //     vision_tx.start_state = 4;
        //     dart_lib.runtime.game_status_stable = 4;
        //     vision_tx.start_state_char = 'R'; //R代表开始比赛，这里多发送一个字符串保证视觉数据正确
        // }
        // else {
        //     dart_lib.runtime.game_status_stable = 0;
        //     vision_tx.start_state = 0;
        //     vision_tx.start_state_char = '\0';
        // }

        //遥控器offline保护和visionrx数据异常的灯控提示
        const bool remoter_offline_safety = remoter.offline && !autoAim_control;
        if (remoter_offline_safety)
        {
            cmd.action = DART_RELAX;
            dart_lib.Update_Fired_State(&lch2sys);
        }
        else
        {
            Resolve_Final_Command(autoAim_control, remoter, vision_rx, &dart_lib, &cmd);
        }

        //更新dart_lib的历史数据
        dart_lib.Update_History(&lch2sys);

        //处理vision_rx数据异常的LED提示
        if (vision_rx.header != 0xA5 || vision_rx.distance == 0.0f ||  vision_rx.checksum == 0)
        {
            tx_semaphore_put(&VisionErrorSem);
        }

        om_publish(cmd_topic, &cmd, sizeof(msg_cmd_t), true, false);
        om_publish(visiontx_topic, &vision_tx, sizeof(msg_visiontx_t),true, false);
        tx_thread_sleep(1);
    };
}

/**
 * @brief 根据当前固定值或表中视觉距离解析出当前的的目标tension_kg和yaw
 * @param dart 
 * @param vision_distance 
 * @return DartRuntime::AimTarget
 */
DartRuntime::AimTarget Resolve_Current_Aim_Target(const DartLibrary& dart, float vision_distance)
{
    int id = dart.runtime.current_dart_id;
    DartRuntime::AimTarget target;
    if (dart.runtime.referee.chosen_target == 0) //前哨站
    {
        target.yaw_offset = dart.config.dart[id].yaw_offset;
        target.tension_kg = dart.config.dart[id].tension_kg_outpost;
    }
    else
    {
#ifdef FORCE_TABLING
        target.yaw_offset = dart.config.dart[id].yaw_offset;
        target.tension_kg = dart.config.dart[id].tension_kg_base;
#else
        Dart_Base_Aim_t base_aim = dart.Get_Base_Aim_By_Distance(id, vision_distance);
        target.yaw_offset = base_aim.yaw_offset;
        target.tension_kg = base_aim.tension_kg;
#endif
    }

    return target;
}

/**
 * @brief 处理遥控器输入，生成对应的cmd命令
 * @param remoter 
 * @param tension_kg
 * @param cmd 
 */
void Build_Remoter_Command(const msg_remoter_t& remoter, float tension_kg, msg_cmd_t* cmd)
{
    if (remoter.left_sw == Mid && remoter.right_sw == M2U)
    {
        cmd->action = DART_FIRE;
        cmd->yaw = remoter.right_x;
        cmd->tension_kg = tension_kg;
    }
    else if (remoter.left_sw == Down)
    {
        if (remoter.right_sw == Down)
        {
            cmd->action = DART_RELAX;
        }
        else if (remoter.right_sw == Mid)
        {
            cmd->action = DART_SYN_ADJUST;
            cmd->rc_syn =  - remoter.right_y * 0.01f;
        }
        else if (remoter.right_sw == Up)
        {
            cmd->action = DART_STRING_ADJUST;
            cmd->rc_string_L = remoter.left_y;
            cmd->rc_string_R = remoter.right_y;
        }
    }
    else if (remoter.left_sw == Mid)
    {
        if (remoter.right_sw == Down)
        {
            cmd->action = DART_YAW_ADJUST;
            cmd->yaw = remoter.right_x;
            if (remoter.left_x > 0.7f || remoter.left_x < -0.7f)
            {
                cmd->action = DART_TRIGGER_OPEN;
            }
            else {
                cmd->action = DART_TRIGGER_CLOSE;
            }
        }
        else if (remoter.right_sw == Mid)
        {
            cmd->action = DART_PREPARE;
            cmd->yaw = remoter.right_x;
            cmd->tension_kg = tension_kg;

        }
        else if (remoter.right_sw == Up)
        {
            cmd->action = DART_FIRE;
            cmd->yaw = remoter.right_x;
            cmd->tension_kg = tension_kg;
        }
    }
    else
    {
        cmd->action = DART_RELAX;

    }
}

/**
 * @brief 处理cmd，判断是否进入autocontrol模式，并根据当前状态生成最终的cmd命令。
 * @todo 加入上位机控制逻辑
 * @param autoAim_control 
 * @param remoter 
 * @param vision_rx 
 * @param dart 
 * @param cmd 
 */
void Resolve_Final_Command(bool autoAim_control,const msg_remoter_t& remoter,const msg_visionrx_t& vision_rx,
                            DartLibrary* dart,msg_cmd_t* cmd)
{
    if (autoAim_control)
    {
        if (dart->runtime.game_status_stable == 4 &&
            dart->runtime.autoAim.autoaim_allow &&
            dart->runtime.fired_count_this_open < 2)
        {
            Run_Auto_Control(&vision_rx, dart, cmd);
        }
        else
        {
            cmd->action = DART_PRE_TENSION;
            cmd->tension_kg = dart->config.pre_tension_kg;
        }
    }
    else
    {
        Build_Remoter_Command(remoter, dart->runtime.current_aim_target.tension_kg, cmd);
    }
}



/**
 * @brief Auto Mode，主要的视觉自动逻辑，更新cmd命令，进行最终的发射条件判断
 * 调整yaw，根据各条件判断是否可以发射
 */
void Run_Auto_Control(const msg_visionrx_t* rx,DartLibrary* dart,  msg_cmd_t* cmd)
{
    dart->runtime.autoAim.running = true;
    //比赛开始之前都不执行自动模式
    // if (dart->runtime.referee.game_status != 4)
    // {
    //     cmd->action = DART_RELAX;
    //     return;
    // }

    if (!(dart->runtime.current_shot_number >= 1 && dart->runtime.current_shot_number <= 4))
    {
        cmd->action = DART_RELAX;
        cmd->tension_kg = dart->runtime.current_aim_target.tension_kg;
        cmd->yaw = 0;
        return;
    }

    cmd->action = DART_PREPARE;
    cmd->tension_kg = dart->runtime.current_aim_target.tension_kg;

    dart->runtime.autoAim.yaw_ok = false;
    if (rx->yaw == 666)
    {
        cmd->yaw = 0;
        return;
    }

    if      (rx->yaw > 0.08f)
    {
        cmd->yaw = 1.0f;
    }
    else if (rx->yaw < -0.08f)
    {
        cmd->yaw = -1.0f;
    }
    else if (rx->yaw > 0.05f)
    {
        cmd->yaw = 0.5f;
    }
    else if (rx->yaw < -0.05f)
    {
        cmd->yaw = -0.5f;
    }
    else if (rx->yaw > 0.015f)
    {
        cmd->yaw = 0.15f;
    }
    else if (rx->yaw < -0.015f)
    {
        cmd->yaw = -0.15f;
    }
    else if (rx->yaw <= 0.015f && rx->yaw >= -0.015f)
    {
        dart->runtime.autoAim.yaw_ok = true;
    };

    //最终的发射条件判断，门开着，当前门打开时发射数量小于2，yaw调整到位，视觉数据稳定
    if ((dart->runtime.vision_door_status == DOOR_OPEN &&
        dart->runtime.fired_count_this_open < 2 &&
        dart->runtime.autoAim.yaw_ok &&
        rx->stable_state == 1))
    {
        cmd->action = DART_FIRE;
    }
};

/**
 * @brief 处理裁判系统数据
 * 
 * @param referee_rx 
 * @param dart 
 */
void Update_referee_data(msg_referee_t* referee_rx, DartLibrary* dart)
{
    dart->runtime.referee.last_launch_station_status = dart->runtime.referee.launch_station_status;
    dart->runtime.referee.game_status =  referee_rx->GameStatus.Game_progress;
    dart->runtime.referee.shooting_remaining_time =  referee_rx->DartInfo.dart_remaining_time;
    dart->runtime.referee.chosen_target = ( referee_rx->DartInfo.dart_info >> 6) & 0x07;
    dart->runtime.referee.launch_station_status =  referee_rx->DartClientCmd.dart_launch_opening_status;

    //26赛季裁判系统状态不稳定，增加雷达数据解析来判断比赛状态，但未使用，先保留代码
    const RoboInteractData_t* payload = &referee_rx->RoboInteractData;
    if (payload->data_cmd_id != 0x0201)
    {
        return;
    }

    const bool is_blue =
        payload->sender_id == BlueRadar &&
        (payload->receiver_id == BlueSentry || payload->receiver_id == BlueDart);
    const bool is_red =
        payload->sender_id == RedRadar &&
        (payload->receiver_id == RedSentry || payload->receiver_id == RedDart);
    if (!is_blue && !is_red)
    {
        return;
    }

    dart->runtime.game_status_ladar = payload->event == 1;
}


DartLibrary::DartLibrary()
{
    config.config_revision = 0;
    config.dart[0] = {0, 0.0f, 0.0f, 0.0f};
    config.dart[1] = {1, 0.0f, 0.0f, 0.0f};
    config.dart[2] = {2, 0.0f, 0.0f, 0.0f};
    config.dart[3] = {3, 0.0f, 0.0f, 0.0f};
    config.dart[4] = {4, 0.0f, 0.0f, 0.0f};
    config.dart[5] = {5, 0.0f, 0.0f, 0.0f};
    config.dart[6] = {6, 0.0f, 0.0f, 0.0f};
    config.dart[7] = {7, 0.0f, 0.0f, 0.0f};
    config.dart[8] = {8, 0.0f, 0.0f, 0.0f};
    config.dart[9] = {9, 0.0f, 0.0f, 0.0f};
    config.dart[10] = {10, 0.0f, 0.0f, 0.0f};
    config.dart[11] = {11, 0.0f, 0.0f, 0.0f};
    config.dart[12] = {12, 0.0f, 0.0f, 0.0f};
    config.dart[13] = {13, 0.0f, 0.0f, 0.0f};
    config.dart[14] = {14, 0.0f, 0.0f, 0.0f};
    config.dart[15] = {15, 0.0f, 0.0f, 0.0f};
    config.dart[16] = {16, 0.0f, 0.0f, 0.0f};

    config.base_distance_table_len = 0;
    config.sequence[0] = 1;
    config.sequence[1] = 2;
    config.sequence[2] = 3;
    config.sequence[3] = 4;
    config.pre_tension_kg = 0.0f;

    runtime.current_shot_number = 1;
    runtime.current_dart_id = config.sequence[0];
    runtime.current_aim_target = {0.0f, 0.0f};
    runtime.vision_door_status = DOOR_CLOSED;
    runtime.last_vision_door_status = DOOR_CLOSED;
    runtime.last_fire_finished = false;
    runtime.fired_count_this_open = 0;

    runtime.game_status_ladar = false;
    runtime.game_status_char = '\0';

    runtime.referee.game_status = 0;
    runtime.referee.shooting_remaining_time = 0;
    runtime.referee.chosen_target = 0;
    runtime.referee.launch_station_status = 1;
    runtime.referee.last_launch_station_status = 1;

    runtime.autoAim.enable = false;
    runtime.autoAim.yaw_ok = false;
    runtime.autoAim.light_lost = false;
    runtime.autoAim.running = false;
    runtime.autoAim.autoaim_allow = false;
    runtime.autoAim.last_autoaim_allow = false;
    runtime.autoAim.referee_launch_closed_stable = false;
    runtime.autoAim.referee_launch_open_stable = false;
    runtime.autoAim.vision_door_closed_stable = false;
    runtime.autoAim.vision_door_open_stable = false;
}

/**
 * @brief 结合visionrx数据与referee的发射台状态共同判断门的状态，更新门状态相关的runtime数据
 * 
 * @param rx 
 */
void DartLibrary::UPDATE_DOOR_STATUS(msg_visionrx_t* rx)
{
    if (rx->light_detected == 1 || rx->light_detected == 2) 
    {
        runtime.vision_door_status = DOOR_OPEN;
    }
    else if (rx->light_detected == 0 || rx->light_detected == 3 )
    {
        runtime.vision_door_status = DOOR_CLOSED;
    }

    //连续10tick视觉门状态为视为vision_door_open_stable = true
    if (runtime.vision_door_status != DOOR_OPEN)
    {
        runtime.autoAim.vision_door_open_stable = false;
        runtime.autoAim.vision_door_open_delay.Reset();
    }
    else if (!runtime.autoAim.vision_door_open_stable &&
             runtime.autoAim.vision_door_open_delay.ReachStable(true, 10))
    {
        runtime.autoAim.vision_door_open_stable = true;
    }

    //连续700tick视觉门状态为关视为vision_door_closed_stable = true。700是测试过的值(防止飞镖飞过摄像头的误判）)
    if (runtime.vision_door_status != DOOR_CLOSED)
    {
        runtime.autoAim.vision_door_closed_stable = false;
        runtime.autoAim.vision_door_closed_delay.Reset();
    }
    else if (!runtime.autoAim.vision_door_closed_stable &&
             runtime.autoAim.vision_door_closed_delay.ReachStable(true, 700))
    {
        runtime.autoAim.vision_door_closed_stable = true;
    }

    //连续10tick裁判系统发射台开视为referee_launch_open_stable = true
    if (runtime.referee.launch_station_status != 0)
    {
        runtime.autoAim.referee_launch_open_stable = false;
        runtime.autoAim.referee_launch_open_delay.Reset();
    }
    else if (!runtime.autoAim.referee_launch_open_stable &&
             runtime.autoAim.referee_launch_open_delay.ReachStable(true, 10))
    {
        runtime.autoAim.referee_launch_open_stable = true;
    }

    //连续10tick裁判系统发射台关视为referee_launch_closed_stable = true
    if (runtime.referee.launch_station_status != 1)
    {
        runtime.autoAim.referee_launch_closed_stable = false;
        runtime.autoAim.referee_launch_closed_delay.Reset();
    }
    else if (!runtime.autoAim.referee_launch_closed_stable &&
             runtime.autoAim.referee_launch_closed_delay.ReachStable(true, 10))
    {
        runtime.autoAim.referee_launch_closed_stable = true;
    }
}

void DartLibrary::Update_AutoAim_Prepare_Allowed()
{
    //视觉门开 or 裁判系统发射台开，视为门开
    if (runtime.autoAim.door_open_delay.ReachStable(runtime.autoAim.vision_door_open_stable || runtime.autoAim.referee_launch_open_stable, 25))
    {
        if (!runtime.autoAim.autoaim_allow)
        {
            runtime.fired_count_this_open = 0;
        }
        runtime.autoAim.autoaim_allow = true;
    }
    //视觉门关 and 裁判系统发射台关，视为门关
    if (runtime.autoAim.door_closed_delay.ReachStable(runtime.autoAim.referee_launch_closed_stable && runtime.autoAim.vision_door_closed_stable, 25))
    {
        runtime.autoAim.autoaim_allow = false;
    }



}
/**
 * @brief 创建或更新飞镖距离表，表中每个点包含了对应距离的yaw_offset和tension_kg数据
 * 
 * @param table 
 * @param table_len 
 */
void DartLibrary::Set_Base_Distance_Table(const Dart_Base_Table_Point_t* table, uint16_t table_len)
{
    config.base_distance_table_len = table_len;
    if (config.base_distance_table_len > BASE_DISTANCE_TABLE_MAX)
    {
        config.base_distance_table_len = BASE_DISTANCE_TABLE_MAX;
    }

    for (uint16_t i = 0; i < config.base_distance_table_len; i++)
    {
        config.base_distance_table[i] = table[i];
    }
}

/**
 * @brief 查表或插值计算出对应距离的yaw_offset和tension_kg数据
 * 若表中没有对应dart_id的数据，或距离数据异常，则返回dart配置中的固定值
 * 
 * @param id 
 * @param distance 
 * @return Dart_Base_Aim_t 
 */
Dart_Base_Aim_t DartLibrary::Get_Base_Aim_By_Distance(int id, float distance) const
{
    int dart_id = (id >= 1 && id <= 16) ? id : 0;
    Dart_Base_Aim_t aim = {config.dart[dart_id].yaw_offset, config.dart[dart_id].tension_kg_base};
    if (dart_id == 0 || distance <= 0.0f || config.base_distance_table_len == 0)
    {
        return aim;
    }

    const Dart_Base_Table_Point_t* lower = nullptr;
    const Dart_Base_Table_Point_t* upper = nullptr;

    for (uint16_t i = 0; i < config.base_distance_table_len; i++)
    {
        const Dart_Base_Table_Point_t* point = &config.base_distance_table[i];
        if (point->id != dart_id)
        {
            continue;
        }

        if (point->distance <= distance &&
            (lower == nullptr || point->distance > lower->distance))
        {
            lower = point;
        }

        if (point->distance >= distance &&
            (upper == nullptr || point->distance < upper->distance))
        {
            upper = point;
        }
    }

    if (lower == nullptr && upper == nullptr)
    {
        return aim;
    }
    if (lower == nullptr)
    {
        return {upper->yaw_offset, upper->tension_kg};
    }
    if (upper == nullptr)
    {
        return {lower->yaw_offset, lower->tension_kg};
    }
    if (upper->distance == lower->distance)
    {
        return {lower->yaw_offset, lower->tension_kg};
    }

    float k = (distance - lower->distance) / (upper->distance - lower->distance);
    aim.yaw_offset = lower->yaw_offset + (upper->yaw_offset - lower->yaw_offset) * k;
    aim.tension_kg = lower->tension_kg + (upper->tension_kg - lower->tension_kg) * k;
    return aim;
}

/**
 * @brief 获取当前换弹位
 * 
 * @return DART_SLOT 
 */
DART_SLOT DartLibrary::Get_Prepare_Slot() const
{
    switch (runtime.current_shot_number)
    {
        case 1:
            return DART_SLOT_NONE;
        case 2:
            return DART_SLOT_1;
        case 3:
            return DART_SLOT_2;
        case 4:
            return DART_SLOT_3;
        default:
            return DART_SLOT_NONE;
    }
}

/**
 * @brief 更新current_dart_id
 * 
 */
void DartLibrary::Update_Current_Dart_Id()
{
    if (runtime.current_shot_number >= 1 && runtime.current_shot_number <= 4)
    {
        runtime.current_dart_id = config.sequence[runtime.current_shot_number - 1];
    }
    else
    {
        runtime.current_dart_id = 0;
    }
}

/**
 * @brief 更新当前状态，每循环判断发射完成上升沿来增加current_shot_number，并更新current_dart_id
 * @param msg 
 */
void DartLibrary::Update_Current_State(msg_launcher2sysctrl_t* msg)
{
    if (msg->is_fire_finished && !runtime.last_fire_finished)
    {
        runtime.current_shot_number++;
        Update_Current_Dart_Id();
    }

}

/**
 * @brief 更新已发射状态
 * 
 * @param msg 
 */
void DartLibrary::Update_Fired_State(msg_launcher2sysctrl_t* msg)
{
    if (msg->is_fire_finished && !runtime.last_fire_finished)
    {
        runtime.fired_count_this_open++;
    }

    // if (last_door_status == DOOR_CLOSING && door_status == DOOR_CLOSED)
    // {
    //     fired_count_this_open = 0;
    // }
    //用判断stable后的视觉逻辑来重置发射计数
    // Reset moved to the next door-open allow edge.
    // if (!autoAim.autoaim_allow && autoAim.last_autoaim_allow)
    // {
    //     fired_count_this_open = 0;
    // }
    //直接用视觉数据
    // if (last_door_status != DOOR_CLOSED && door_status == DOOR_CLOSED)
    // {
    //     fired_count_this_open = 0;
    // }

}

void DartLibrary::Update_History(msg_launcher2sysctrl_t* msg)
{
    runtime.last_vision_door_status = runtime.vision_door_status;
    runtime.last_fire_finished = msg->is_fire_finished;
    runtime.autoAim.last_autoaim_allow = runtime.autoAim.autoaim_allow;
}
