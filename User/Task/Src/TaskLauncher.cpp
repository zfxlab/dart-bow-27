/**
 * @file TaskLauncher.cpp
 * @author Aeskyrr17
 * @brief 发射状态机和控制逻辑
 * @todo void Dart_Load(msg_sensor_t* sensor);
 * @todo launcher_status_t的使用（暂未确定）
 */
#include "main.h"
#include "tx_api.h"

#include "om.h"
#include "magicmsgs.hpp"
#include "config_launcher.hpp"
#include "config_motor.hpp"
#include "math.hpp"

TX_THREAD LauncherThread;
uint8_t LauncherThreadStack[2048] = {0};

Launcher_Cxt_t launcher{};

//global variable for debug
msg_motorfdb_t debug_motorfdb{};
// msg_launcher2sysctrl_t lch2sys{};
// msg_cmd_t cmd{};
// msg_sensor_t sensor{};
// msg_motor_ctrl_t motorctrl{};


[[nonreturn]] void LauncherThreadFun(ULONG initial_input) 
{
    UNUSED(initial_input); 
    om_topic_t *motorctrl_topic = om_config_topic(nullptr, "ca", "motorctrl", sizeof(msg_motor_ctrl_t));
    msg_motor_ctrl_t motorctrl{};
    om_topic_t *lch2sys_topic = om_config_topic(nullptr, "ca", "lch2sys", sizeof(msg_launcher2sysctrl_t));
    msg_launcher2sysctrl_t lch2sys{};

    om_suber_t *cmd_suber = om_subscribe(om_find_topic("cmd", UINT32_MAX));
    msg_cmd_t cmd{};
    om_suber_t *sensor_suber = om_subscribe(om_find_topic("sensor", UINT32_MAX));
    msg_sensor_t sensor{};
    om_suber_t *motorfdb_suber = om_subscribe(om_find_topic("motorfdb", UINT32_MAX));
    msg_motorfdb_t motorfdb{};


    const float gantry_pos_deadzone = 0.03f;
    const float syn_pos_deadzone = 0.7f;
    const float string_tension_deadzone_kg = 0.05f;
    const float string_relax_spd = 1.0f; //副弦慢速放松，norm
    const float string_relax_min_tension_kg = 29.0f;

    const float syn_pos_0 = 0.0f;  
    const float syn_pos_1 = -25.2f;
    const float syn_pos_2 = -17.8f;
    const float syn_pos_3 = -36.75f;
    // const float syn_pos_4 = -26.5f; //原本用于“慢速离开扳机”的位置判断，现在暂时不用
    const float syn_pos_5 = 2.08f;

    const float syn_slow_spd = 7.0f;
    const float string_force_error_limit_kg = 10000.0f; //? 暂时没有使用,测试数据
    const float syn_tq_error_limit = 10.0f;
    const float string_force_jump_limit_kg = 50.0f;
    const ULONG string_relax_ticks = 1500;
    const ULONG syn_tq_error_ticks = 5000;
    const ULONG string_force_jump_ticks = 1000;



    motorctrl.Coil_L_spd = 0.0f;
    motorctrl.Coil_R_spd = 0.0f;

    bool hand_trigger_lock = true;
    // bool coil_ready_stopped = false;
    bool trigger_lock_latched = false;
    bool string_force_kg_fdb_valid = false;
    float string_L_force_kg_1s_ago = 0.0f;
    float string_R_force_kg_1s_ago = 0.0f;
    LAUNCHER_FSM_STATE last_fsm_state = LAUNCHER_FSM_STATE_INVALID;
    PREPARE_STATE last_prep_state = PREPARE_STATE_INVALID;

    delay_t trig_lock_delay{};
    delay_t ready_fire_delay{};
    delay_t firing_hold_delay{};
    delay_t syn_tq_error_delay{};
    delay_t string_relax_delay{};
    delay_t string_force_jump_delay{};

    for (;;) 
    {
        om_suber_export(cmd_suber, &cmd, false);
        om_suber_export(sensor_suber, &sensor, false);
        om_suber_export(motorfdb_suber, &motorfdb, false);

        sensor.is_coil_reset = sensor.is_coil_L_reset && sensor.is_coil_R_reset;

        //! 测试用
        // sensor.is_string_tight = true;
        // sensor.is_door_open = true;
        // lch2sys.next_dart_slot = cmd.next_dart_slot;

        // motorctrl.trigger_lock = true;
        motorctrl.trigger_release = false;
        motorctrl.Coil_mode = SPD;
        motorctrl.Coil_L_mode = SPD;
        motorctrl.Coil_R_mode = SPD;
        motorctrl.Coil_L_spd = 0.0f;
        motorctrl.Coil_R_spd = 0.0f;
        motorctrl.string_L_spd = 0.0f;
        motorctrl.string_R_spd = 0.0f;
        motorctrl.gantry_target_slot = DART_SLOT_NONE;
        motorctrl.synbelt_mode = POS;
        motorctrl.synbelt_spd = 0.0f;
        motorctrl.string_L_tension_kg = cmd.tension_kg;
        motorctrl.string_R_tension_kg = cmd.tension_kg;  //!要确定一下一开始需要张紧到多少是由谁决定的?或者不这么写？？？
        motorctrl.string_able = false;


        //直接处理yaw
        motorctrl.yaw_spd = cmd.yaw*0.1f;

        //todo: 处理error信息[to test]
        bool string_force_error = (Numeric::abs(sensor.string_L_force_kg) > string_force_error_limit_kg) ||
                                  (Numeric::abs(sensor.string_R_force_kg) > string_force_error_limit_kg);
        bool string_force_jump_error = false;
        bool syn_tq_error = syn_tq_error_delay.ReachStable(
            Numeric::abs(motorfdb.syn_tq_fdb) > syn_tq_error_limit,
            syn_tq_error_ticks);

        if (!string_force_kg_fdb_valid || launcher.fsm_state == FIRING)
        {
            string_force_kg_fdb_valid = true;
            string_L_force_kg_1s_ago = sensor.string_L_force_kg;
            string_R_force_kg_1s_ago = sensor.string_R_force_kg;
            string_force_jump_delay.Reset();
        }
        else
        {
            string_force_jump_error =
                ((Numeric::abs(sensor.string_L_force_kg - string_L_force_kg_1s_ago) > string_force_jump_limit_kg) ||
                (Numeric::abs(sensor.string_R_force_kg - string_R_force_kg_1s_ago) > string_force_jump_limit_kg))
                &&
                string_L_force_kg_1s_ago != 0.0f && string_R_force_kg_1s_ago != 0.0f; //避免初始状态力传感器数据异常导致的误判

            if (string_force_jump_delay.Reach(string_force_jump_ticks))
            {
                string_L_force_kg_1s_ago = sensor.string_L_force_kg;
                string_R_force_kg_1s_ago = sensor.string_R_force_kg;
            }
        }
        //! 暂时不判断力传感器的跳变
               string_force_jump_error = false;


        //处理cmd以及各个fsm的优先级及切换
        if (string_force_error || syn_tq_error || string_force_jump_error)
        {
            launcher.fsm_state = ERROR_STOP;
        }
        else if (launcher.fsm_state != ERROR_STOP &&
                 launcher.fsm_state != FIRING &&
                 launcher.fsm_state != PREPARING &&
                 launcher.fsm_state != READY &&
                 cmd.action == DART_PRE_TENSION)
        {
            launcher.fsm_state = PRE_TENSION;
        }
        else if (launcher.fsm_state != ERROR_STOP &&
                 cmd.action == DART_RELAX)
        {
            if ((launcher.fsm_state != HAND_CONTROL || hand_trigger_lock) &&
                launcher.fsm_state != FIRING)
                launcher.fsm_state = IDLE;
        }
        else if (cmd.action == DART_SYN_ADJUST || cmd.action == DART_STRING_ADJUST ||
          cmd.action == DART_TRIGGER_OPEN || cmd.action == DART_TRIGGER_CLOSE ||
          cmd.action == DART_YAW_ADJUST)
        {
            // 如果遥控器发出了手动调试指令，强行切入手动状态
            launcher.fsm_state = HAND_CONTROL;
        }

        // Freeze the state at loop entry; state changes below take effect on the next loop.
        //和"current_state"做区分，用于区分是否是“刚进入一个状态”
        const LAUNCHER_FSM_STATE current_fsm_state = launcher.fsm_state;
        const PREPARE_STATE current_prep_state = launcher.prep_state;
        const bool fsm_state_changed = (current_fsm_state != last_fsm_state);
        const bool prep_state_changed = fsm_state_changed || (current_prep_state != last_prep_state);

        if (prep_state_changed)
        {
            // coil_ready_stopped = false;
            trigger_lock_latched = false;
        }

        //FSM具体实现逻辑   
        switch (current_fsm_state)
        {
            case HAND_CONTROL:
                motorctrl.trigger_release = hand_trigger_lock ? false : true; //! 可能需要调整
                motorctrl.synbelt_pos += 0;
                motorctrl.string_L_spd = 0.0f;
                motorctrl.string_R_spd = 0.0f;
                motorctrl.string_able = false;
                if (cmd.action == DART_SYN_ADJUST)
                {
                    motorctrl.synbelt_pos += cmd.rc_syn;
                }
                else if (cmd.action == DART_STRING_ADJUST)
                {
                    motorctrl.string_L_spd = cmd.rc_string_L;
                    motorctrl.string_R_spd = cmd.rc_string_R;
                }
                else if (cmd.action == DART_YAW_ADJUST)
                {
                    motorctrl.yaw_spd = cmd.yaw*0.1f;
                }
                else if (cmd.action == DART_FIRE)
                {
                    launcher.fsm_state = FIRING;
                    break;
                }
                else if (cmd.action == DART_PREPARE)
                {
                    launcher.fsm_state = PREPARING;
                    launcher.current_slot = cmd.next_dart_slot;
                    launcher.prep_state = RETRACT;
                }
                else if (cmd.action == DART_TRIGGER_CLOSE)
                {
                    hand_trigger_lock = true;
                    motorctrl.trigger_release = false;
                }
                else if (cmd.action == DART_TRIGGER_OPEN)
                {
                    hand_trigger_lock = false;
                    motorctrl.trigger_release = true;
                }

                break;
                
            case IDLE:
                // motorctrl.yaw_spd = 0.0f;
                
                motorctrl.trigger_release = false;
                motorctrl.gantry_target_slot = DART_SLOT_NONE;//龙门架在默认位置
                motorctrl.string_able = false;
                motorctrl.synbelt_mode = POS;
                // motorctrl.synbelt_pos = syn_pos_0;
                motorctrl.synbelt_pos +=0 ; 

                if (cmd.action == DART_PREPARE) 
                {
                    launcher.fsm_state = PREPARING;
                    launcher.current_slot = cmd.next_dart_slot;
                    launcher.prep_state = RETRACT;
                }
                break;

            case PRE_TENSION:
            {

                motorctrl.string_able = true;
                motorctrl.string_L_tension_kg = cmd.tension_kg;
                motorctrl.string_R_tension_kg = cmd.tension_kg;
                motorctrl.synbelt_mode = POS;
                motorctrl.synbelt_pos +=0 ;

                if (cmd.action == DART_PREPARE)
                {
                    launcher.fsm_state = PREPARING;
                    launcher.current_slot = cmd.next_dart_slot;
                    launcher.prep_state = RETRACT;
                }
                else if (cmd.action == DART_RELAX)
                {
                    launcher.fsm_state = IDLE;
                }
                break;
            }

            case PREPARING:

                switch (current_prep_state)
                {
                    case RETRACT:
                    {
                        motorctrl.synbelt_mode = POS;
                        motorctrl.synbelt_pos +=0 ;
                        motorctrl.string_able = false;
                        motorctrl.string_L_spd = (sensor.string_L_force_kg > string_relax_min_tension_kg) ? string_relax_spd : 0.0f;
                        motorctrl.string_R_spd = (sensor.string_R_force_kg > string_relax_min_tension_kg) ? string_relax_spd : 0.0f;

                        if (string_relax_delay.Reach(1500, prep_state_changed))
                        {
                            launcher.prep_state = SYN_1;
                        }
                        break;
                    }
                    case SYN_1:
                        motorctrl.synbelt_mode = POS;
                        motorctrl.synbelt_pos = syn_pos_1;

                        if (cmd.current_shot_number == 1)
                        {
                            launcher.prep_state = SYN_TRIGGER_READY;    //第一发镖直接上膛
                            break;
                        }
                        if (Numeric::abs(motorfdb.syn_pos_fdb - syn_pos_1) <= syn_pos_deadzone)
                        {
                            launcher.prep_state = GANTRY_1;
                        }
                        break;

                    case GANTRY_1:
                        motorctrl.gantry_target_slot = launcher.current_slot;
                        if (Numeric::abs(Get_Gantry_Target_Pos(launcher.current_slot) - motorfdb.gantry_pos_fdb) <= gantry_pos_deadzone)
                        {
                            launcher.prep_state = SYN_2;
                        };
                        break;
                        
                    case SYN_2:
                        motorctrl.gantry_target_slot = launcher.current_slot;
                        motorctrl.synbelt_mode = SPD;
                        motorctrl.synbelt_spd = syn_slow_spd;
                        motorctrl.synbelt_pos = syn_pos_2;
                        if (Numeric::abs(motorfdb.syn_pos_fdb - syn_pos_2) <= syn_pos_deadzone)
                        {
                            motorctrl.synbelt_spd = 0.0f;
                            motorctrl.synbelt_mode = POS;  
                            launcher.prep_state = GANTRY_2;
                        };
                        break;

                    case GANTRY_2:
                        motorctrl.gantry_target_slot = DART_SLOT_NONE;
                        if (Numeric::abs(Get_Gantry_Target_Pos(DART_SLOT_NONE) - motorfdb.gantry_pos_fdb) <= gantry_pos_deadzone)
                        {
                            launcher.prep_state = SYN_TRIGGER_READY;
                        };
                        break;

                    case SYN_TRIGGER_READY:
                    {
                        motorctrl.synbelt_pos = syn_pos_3;
                        motorctrl.string_able = false;

                        bool syn_reset = Numeric::abs(motorfdb.syn_pos_fdb - syn_pos_3) <= syn_pos_deadzone;


                        // if (trig_lock_delay.Reach(syn_reset, 500, prep_state_changed) 
                        //     && sensor.is_trigger_locked)
                        if (trig_lock_delay.Reach(syn_reset, 250, prep_state_changed))
                        {
                            launcher.prep_state = TENSION_AND_RETRACT_AND_YAW;
                        };
                        break;
                    }
                    case TENSION_AND_RETRACT_AND_YAW:
                    {
                        motorctrl.trigger_release = false;
                        if (motorfdb.syn_pos_fdb >= syn_pos_1)
                        {
                            motorctrl.string_able = true; //保证同步带已经离开弓弦后再开始调整弓弦的力
                        }
                        else
                        {
                            motorctrl.string_able = false;
                        }

                        motorctrl.synbelt_pos = syn_pos_5;
                        motorctrl.synbelt_mode = POS;
                            // motorctrl.synbelt_pos = syn_pos_5;
                            // motorctrl.synbelt_mode = POS;

                        motorctrl.yaw_spd = cmd.yaw;



                        bool string_L_ok = Numeric::abs(sensor.string_L_force_kg - cmd.tension_kg) <= string_tension_deadzone_kg;
                        bool string_R_ok = Numeric::abs(sensor.string_R_force_kg - cmd.tension_kg) <= string_tension_deadzone_kg;
                        bool syn_reset = Numeric::abs(motorfdb.syn_pos_fdb - syn_pos_5) <= syn_pos_deadzone;

                        if (string_L_ok && string_R_ok && syn_reset)
                        {
                            launcher.fsm_state = READY;
                        }
                        else
                        {
                            if (!string_L_ok || !string_R_ok)
                            {
                                motorctrl.string_L_tension_kg = cmd.tension_kg;
                                motorctrl.string_R_tension_kg = cmd.tension_kg;
                            }
                        }                           
                        break;
                    }
                    default:
                        launcher.fsm_state = IDLE;
                        break;                               
                }
                break;

            case READY:
            {
                motorctrl.trigger_release = false;
                motorctrl.string_able = true;
                motorctrl.string_L_tension_kg = cmd.tension_kg;//保持力矩
                motorctrl.string_R_tension_kg = cmd.tension_kg;

                if (cmd.action == DART_FIRE &&
                    ready_fire_delay.Reach(750, fsm_state_changed))
                {
                    launcher.fsm_state = FIRING;
                }
                break;
            }

            case FIRING:
            {
                motorctrl.string_able = false;
                motorctrl.trigger_release = true; //解锁扳机
                launcher.is_fire_done = false;

                if (firing_hold_delay.Reach(500, fsm_state_changed))
                {
                    Mark_Fire_Done(launcher);
                    launcher.fsm_state = IDLE;
                }
                break;
                }

            case ERROR_STOP:
            {
                motorctrl.trigger_release = false;
                motorctrl.string_able = false;
                // motorctrl.gantry_target_slot = DART_SLOT_NONE;
                motorctrl.synbelt_mode = POS;
                motorctrl.synbelt_pos += 0;
                if (cmd.action == DART_SYN_ADJUST || cmd.action == DART_STRING_ADJUST ||
                    cmd.action == DART_TRIGGER_CLOSE || cmd.action == DART_TRIGGER_CLOSE ||
                    cmd.action == DART_YAW_ADJUST)
                {
                    launcher.fsm_state = HAND_CONTROL;
                }
                break;
            }
        }

         //更新历史状态
        lch2sys.current_state = launcher.fsm_state;
        lch2sys.is_fire_finished = launcher.is_fire_done;
        lch2sys.last_fire_finished = launcher.last_fire_done;
        om_publish(lch2sys_topic, &lch2sys, sizeof(msg_launcher2sysctrl_t), true, false);
        om_publish(motorctrl_topic, &motorctrl, sizeof(msg_motor_ctrl_t), true, false);

        Step_Fire_Done(launcher);
        memcpy(&debug_motorfdb, &motorfdb, sizeof(motorfdb));
        last_fsm_state = current_fsm_state;
        last_prep_state = current_prep_state;
        tx_thread_sleep(1);
    }

}
