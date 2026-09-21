#pragma once
#include "DM4310_MultiPos.hpp"
#include "DM8009P.hpp"
#include "DMMotor.hpp"
#ifndef TASK_MOTOR_HPP
#define TASK_MOTOR_HPP

#include "main.h"
#include "stm32h7xx_hal_gpio.h"
#include "gpio.h"
#include "tim.h"
#include "bsp_pwm.hpp"

#include "tx_api.h"

#include "DJIMotorHandler.hpp"
#include "DMMotorHandler.hpp"
#include "M3508.hpp"
#include "M2006.hpp"
#include "DM4310.hpp"

#include "Stepper.hpp"
#include "X_V2.hpp"

#include "config_launcher.hpp"


/** @brief 舵机类，实现两点间移动
 * 长时间一直发送PWM舵机很可能过热，撒放机构有机械自锁，Open和Lock函数会在达到目标位置后停止PWM输出。
 */
class ServoMotors
{
    public:
    enum OpenResetState
    {
        OPEN_RESET_IDLE = 0,
        OPEN_RESET_OPENING,
        OPEN_RESET_RESETTING,
    };

    TIM_HandleTypeDef* htim; 
    uint32_t channel;

    float open_pulse; //打开时脉冲
    float idle_pulse; //空闲时脉冲

    float last_pulse;
    bool pwm_stopped;
    OpenResetState open_reset_state;

    delay_t open_delay;
    delay_t reset_delay;
    static constexpr ULONG pwm_hold_ticks = 1000;

    ServoMotors()
    {
        this->htim = nullptr;
        this->channel = 0;
        this->open_pulse = 1050.0  / 20000.0f;
        this->idle_pulse = 1500.0 / 20000.0f;//50Hz //1750
        this->last_pulse = 0.0f;
        this->pwm_stopped = false;
        this->open_reset_state = OPEN_RESET_IDLE;

    }

    void Init(TIM_HandleTypeDef* htim, uint32_t channel) //初始化舵机,配置挂载的定时器和通道
    {
        this->htim = htim;
        this->channel = channel;
        PWM_Start(this->htim, this->channel);
        PWM_SetDutyRatio(this->htim, idle_pulse, this->channel);
        this->last_pulse = this->idle_pulse;
        this->pwm_stopped = false;
        this->open_reset_state = OPEN_RESET_IDLE;
    }

    bool OpenAndReset()
    {
        switch (this->open_reset_state)
        {
            case OPEN_RESET_IDLE:
                this->open_delay.Reset();
                this->reset_delay.Reset();
                this->pwm_stopped = false;
                this->open_reset_state = OPEN_RESET_OPENING;
                break;

            case OPEN_RESET_OPENING:
                ApplyPulse(this->open_pulse);
                if (this->open_delay.ReachLatched(pwm_hold_ticks))
                {
                    this->reset_delay.Reset();
                    this->open_reset_state = OPEN_RESET_RESETTING;
                }
                break;

            case OPEN_RESET_RESETTING:
                ApplyPulse(this->idle_pulse);
                if (this->reset_delay.ReachLatched(pwm_hold_ticks))
                {
                    PWM_Stop(this->htim, this->channel);
                    this->pwm_stopped = true;
                    this->open_delay.Reset();
                    this->reset_delay.Reset();
                    this->last_pulse = this->idle_pulse;
                    this->open_reset_state = OPEN_RESET_IDLE;
                    return true;
                }
                break;

            default:
                this->open_reset_state = OPEN_RESET_IDLE;
                break;
        }
        return false;
    }

    // void OpenThenRelax()
    // {
    //     ApplyPulseForHold(this->open_pulse, this->open_delay);
    // }

    // void LockThenRelax()
    // {
    //     ApplyPulseForHold(this->idle_pulse, this->reset_delay);
    // }

    // void OpenRemain()
    // {
    //     ApplyPulse(this->open_pulse);
    // }

    // void LockRemain()
    // {
    //     ApplyPulse(this->idle_pulse);
    // }

    void ApplyPulseForHold(float target_pulse, delay_t& hold_delay)
    {
        if (this->last_pulse != target_pulse)
        {
            hold_delay.Reset();
            this->last_pulse = target_pulse;
            this->pwm_stopped = false;
        }

        if (this->pwm_stopped)
        {
            return;
        }

        PWM_Start(this->htim, this->channel);
        PWM_SetDutyRatio(this->htim, target_pulse, this->channel);

        if (hold_delay.ReachLatched(pwm_hold_ticks))
        {
            PWM_Stop(this->htim, this->channel);
            this->pwm_stopped = true;
        }
    }

    void ApplyPulse(float target_pulse)
    {

        PWM_Start(this->htim, this->channel);
        PWM_SetDutyRatio(this->htim, target_pulse, this->channel);
    }
};


/**
 * @brief 张大头步进电机类
 * @details 具体的实现功能来源于张大头步进电机例程，需修改static变量
 * 
 */
class ZDTStepper
{
    public:
    ZDTStepper()
    {
        this->id = 0;
        this->dir = 0;
        this->positive_dir = 0;
        this->hcan = nullptr;
    }

    uint16_t torque;
    uint16_t speed;
    uint32_t position;
    float current;

    uint8_t id;
    uint8_t dir;
    uint8_t positive_dir;//正方向定义，通过驱动板修改
    FDCAN_HandleTypeDef* hcan;

    void Init(FDCAN_HandleTypeDef* hcan, uint8_t id, uint8_t positive_dir)
    {
        this->hcan = hcan;
        this->id = id;
        this->positive_dir = positive_dir;      //在驱动板中设置的正方向
        this->dir = positive_dir;
    }

    /**
     * @brief 根据带符号速度解析当前方向，并返回发送给驱动板的速度绝对值
     * @param signed_speed  正负号表示通过电机驱动板设定的方向，正负与底层0/1的对应关系由positive_dir决定
     */
    float ParseSpeed(float signed_speed)
    {
        if (signed_speed >= 0.0f)
            this->dir = this->positive_dir;
        else
            this->dir = (this->positive_dir == 0) ? 1 : 0;

        return ABS(signed_speed);
    }

    void SendSpd(uint16_t acc, float signed_speed, bool snF, uint16_t maxCur)
    {
        const float abs_speed = this->ParseSpeed(signed_speed);
        this->X_V2_Vel_LC_Control(this->id, this->dir, acc, abs_speed, snF, maxCur);
    }

/**
 * @brief 在canrxcallback中调用，筛选不同功能码id并给参数赋值
 * 
 * @param hfdcan 
 * @param rx_data uint8_t[8]的rxdata
 * @param rxid rx_header.Identifier
 */
    void updateFeedback(FDCAN_HandleTypeDef *hfdcan, uint8_t *rx_data, uint32_t rxid)
    {
        uint8_t targetid = (uint8_t)(rxid >> 8) & 0xFF;

        if (targetid != this->id) return;

        switch (rx_data[0])
        {
            case 0x27:// 读取相电流
                this->torque = (uint16_t)(rx_data[2] << 8 | rx_data[3]);
                break;
            case 0x35:// 读取电机实时转速
                this->speed = (uint16_t)(rx_data[2] << 8 | rx_data[3]);
                break;
            case 0x36:// 读取电机实时位置
                this->position = (uint32_t)((rx_data[2] << 24) | (rx_data[3] << 16) |
                                             (rx_data[4] << 8) | rx_data[5]);
                break;
        };

    }

/**
  * @brief    读取系统参数
  * @param    addr  ：电机地址
  * @param    s     ：系统参数类型，填入对应的宏
  * @retval   地址 + 功能码 + 命令状态 + 校验字节
  */
    void X_V2_Read_Sys_Params(uint8_t addr, SysParams_t s)
    {
        uint8_t i = 0;
        uint8_t cmd[16] = {0};
        
        // 装载命令
        cmd[i] = addr; ++i;                   // 地址

        switch(s)                             // 功能码
        {
            case S_VBUS : cmd[i] = 0x24; ++i; break;	// 读取总线电压
            case S_CBUS : cmd[i] = 0x26; ++i; break;	// 读取总线电流
            case S_CPHA : cmd[i] = 0x27; ++i; break;	// 读取相电流
            case S_ENCO : cmd[i] = 0x29; ++i; break;	// 读取编码器原始值
            case S_CLKC : cmd[i] = 0x30; ++i; break;	// 读取实时脉冲数
            case S_ENCL : cmd[i] = 0x31; ++i; break;	// 读取经过线性化校准后的编码器值
            case S_CLKI : cmd[i] = 0x32; ++i; break;	// 读取输入脉冲数
            case S_TPOS : cmd[i] = 0x33; ++i; break;	// 读取电机目标位置
            case S_SPOS : cmd[i] = 0x34; ++i; break;	// 读取电机实时设定的目标位置
            case S_VEL  : cmd[i] = 0x35; ++i; break;	// 读取电机实时转速
            case S_CPOS : cmd[i] = 0x36; ++i; break;	// 读取电机实时位置
            case S_PERR : cmd[i] = 0x37; ++i; break;	// 读取电机位置误差
            case S_VBAT : cmd[i] = 0x38; ++i; break;	// 读取多圈编码器电池电压（Y42）
            case S_TEMP : cmd[i] = 0x39; ++i; break;	// 读取电机实时温度（X42S/Y42）
            case S_FLAG : cmd[i] = 0x3A; ++i; break;	// 读取电机状态标志位
            case S_OFLAG: cmd[i] = 0x3B; ++i; break;	// 读取回零状态标志位
            case S_OAF  : cmd[i] = 0x3C; ++i; break;	// 读取电机状态标志位 + 回零状态标志位（X42S/Y42）
            case S_PIN  : cmd[i] = 0x3D; ++i; break;	// 读取引脚状态（X42S/Y42）
            case S_SYS  : cmd[i] = 0x43; ++i; cmd[i] = 0x7A; ++i; break;	// 读取系统状态参数
            default: break;
        }

        cmd[i] = 0x6B; ++i;                   // 校验字节
        
        // 发送命令
        can_SendCmd(this->hcan, cmd, i);
    }

/**
* @brief    力矩模式
* @param    addr  	：电机地址
* @param    sign  	：符号（方向）		，0为正，1为负
* @param    t_ramp	：电流斜率(Ma/s)	，范围0 - 65535Ma/s
* @param    torque	：力矩电流(Ma)		，范围0 - 6000Ma
* @param    snF   	：多机同步标志		，false为不启用，true为启用
* @retval   地址 + 功能码 + 命令状态 + 校验字节
*/
    void X_V2_Torque_Control(uint8_t addr, uint8_t sign, uint16_t t_ramp, uint16_t torque, bool snF)
    {
        uint8_t cmd[16] = {0};
    
        // 装载命令
        cmd[0] =  addr;                       // 地址
        cmd[1] =  0xF5;                       // 功能码
        cmd[2] =  sign;                       // 符号（方向）
        cmd[3] =  (uint8_t)(t_ramp >> 8);     // 电流斜率(Ma/s)
        cmd[4] =  (uint8_t)(t_ramp >> 0);
        cmd[5] =  (uint8_t)(torque >> 8);     // 力矩电流(Ma)
        cmd[6] =  (uint8_t)(torque >> 0);
        cmd[7] =  snF;                        // 多机同步标志
        cmd[8] =  0x6B;                       // 校验字节
        
        // 发送命令
        can_SendCmd(this->hcan, cmd, 9);
    }


/**
* @brief    力矩模式限速控制（X42S/Y42）
* @param    addr  	：电机地址
* @param    sign  	：符号（方向）		，0为正，1为负
* @param    t_ramp	：电流斜率(Ma/s)	，范围0 - 65535Ma/s
* @param    torque	：力矩电流(Ma)		，范围0 - 6000Ma
* @param    snF   	：多机同步标志		，false为不启用，true为启用
* @param    maxVel	：最大速度(RPM)	，范围0.0 - 3000.0RPM
* @retval   地址 + 功能码 + 命令状态 + 校验字节
*/
    void X_V2_Torque_LV_Control(uint8_t addr, uint8_t sign, uint16_t t_ramp, uint16_t torque, bool snF, float maxVel)
    {
        uint8_t cmd[16] = {0}; uint16_t v = 0;

        // 将速度放大10倍发送过去
        v = (uint16_t)ABS(maxVel * 10.0f);
        
        // 装载命令
        cmd[0]  =  this->id;                     // 地址
        cmd[1]  =  0xC5;                      // 功能码
        cmd[2]  =  sign;                      // 符号（方向）
        cmd[3]  =  (uint8_t)(t_ramp >> 8);    // 电流斜率(Ma/s)
        cmd[4]  =  (uint8_t)(t_ramp >> 0);
        cmd[5]  =  (uint8_t)(torque >> 8);    // 力矩电流(Ma)
        cmd[6]  =  (uint8_t)(torque >> 0);
        cmd[7]  =  snF;                       // 多机同步标志
        cmd[8]  =  (uint8_t)(v >> 8);    	  // 最大速度(RPM)
        cmd[9]  =  (uint8_t)(v >> 0);    
        cmd[10] =  0x6B;                      // 校验字节
        
        // 发送命令
        can_SendCmd(this->hcan, cmd, 11);
    }

/**
  * @brief    修改PID参数
  * @param    addr     ：电机地址
  * @param    svF      ：是否存储标志，false为不存储，true为存储
  * @param    pTkp 	 	 ：梯形曲线位置环比例系数，默认为126640
	* @param    pBkp 	 	 ：直通限速位置环比例系数，默认为126640
	* @param    vkp 	 	 ：速度环比例系数，42默认为15600
	* @param    vki 	 	 ：速度环积分系数，42默认为26
  * @retval   地址 + 功能码 + 命令状态 + 校验字节
  */
    void X_V2_Modify_PID_Params(bool svF, uint32_t pTkp, uint32_t pBkp, uint32_t vkp, uint32_t vki)
    {
        uint8_t cmd[32] = {0};
    
        // 装载命令
        cmd[0]  =  this->id;                 // 地址
        cmd[1]  =  0x4A;                      // 功能码
        cmd[2]  =  0xC3;                      // 辅助码
        cmd[3]  =  svF;                       // 是否存储标志，false为不存储，true为存储
        cmd[4]  =  (uint8_t)(pTkp >> 24);			// pTkp
        cmd[5]  =  (uint8_t)(pTkp >> 16);
        cmd[6]  =  (uint8_t)(pTkp >> 8);
        cmd[7]  =  (uint8_t)(pTkp >> 0);
        cmd[8]  =  (uint8_t)(pBkp >> 24);			// pBkp
        cmd[9]  =  (uint8_t)(pBkp >> 16);
        cmd[10] =  (uint8_t)(pBkp >> 8);
        cmd[11] =  (uint8_t)(pBkp >> 0);
        cmd[12] =  (uint8_t)(vkp >> 24);			// vkp
        cmd[13] =  (uint8_t)(vkp >> 16);
        cmd[14] =  (uint8_t)(vkp >> 8);
        cmd[15] =  (uint8_t)(vkp >> 0);
        cmd[16] =  (uint8_t)(vki >> 24);			// vki
        cmd[17] =  (uint8_t)(vki >> 16);
        cmd[18] =  (uint8_t)(vki >> 8);
        cmd[19] =  (uint8_t)(vki >> 0);
        cmd[20] =  0x6B;                      // 校验字节
    
        // 发送命令
        can_SendCmd(this->hcan,cmd, 21);
    }

/**
  * @brief    速度模式限电流控制（X42S/Y42）
  * @param    addr  ：电机地址
  * @param    dir   ：方向						，0为CW，1为CCW
  * @param    acc   ：加速度(RPM/s)	，范围0 - 65535RPM/s
  * @param    vel		：速度(RPM)			，范围0.0 - 3000.0RPM
  * @param    snF   ：多机同步标志		，false为不启用，true为启用
	* @param    maxCur：最大电流(mA)		，范围0 - 6000mA
  * @retval   地址 + 功能码 + 命令状态 + 校验字节
  */
    void X_V2_Vel_LC_Control(uint8_t addr, uint8_t dir, uint16_t acc, float vel, bool snF, uint16_t maxCur)
    {
        uint8_t cmd[16] = {0}; uint16_t v = 0;

        // 将速度放大10倍发送过去
        v = (uint16_t)ABS(vel * 10.0f);

        // 装载命令
        cmd[0]  =  this->id;                         // 地址
        cmd[1]  =  0xC6;                      // 功能码
        cmd[2]  =  dir;                       // 符号（方向）
        cmd[3]  =  (uint8_t)(acc >> 8);     	// 加速度(RPM/s)
        cmd[4]  =  (uint8_t)(acc >> 0);
        cmd[5]  =  (uint8_t)(v >> 8);        	// 速度(RPM)
        cmd[6]  =  (uint8_t)(v >> 0);
        cmd[7]  =  snF;                       // 多机同步运动标志
        cmd[8]  =  (uint8_t)(maxCur >> 8);    // 最大电流(mA)
        cmd[9]  =  (uint8_t)(maxCur >> 0);
        cmd[10] =  0x6B;                      // 校验字节
        
        // 发送命令
        can_SendCmd(this->hcan, cmd, 11);
    }


/**
  * @brief    定时返回信息命令（X42S/Y42）
  * @param    addr  	：电机地址
  * @param    s     	：系统参数类型
  * @param    time_ms ：定时时间
  * @retval   地址 + 功能码 + 命令状态 + 校验字节
  */
    void X_V2_Auto_Return_Sys_Params_Timed(uint8_t addr, SysParams_t s, uint16_t time_ms)
    {
        uint8_t i = 0; 
        uint8_t cmd[16] = {0};
    
        // 装载命令
        cmd[i] = this->id; ++i;                   // 地址

        cmd[i] = 0x11; ++i;                   // 功能码

        cmd[i] = 0x18; ++i;                   // 辅助码

        switch(s)                             // 信息功能码
        {
            case S_VBUS : cmd[i] = 0x24; ++i; break;	// 读取总线电压
            case S_CBUS : cmd[i] = 0x26; ++i; break;	// 读取总线电流
            case S_CPHA : cmd[i] = 0x27; ++i; break;	// 读取相电流
            case S_ENCO : cmd[i] = 0x29; ++i; break;	// 读取编码器原始值
            case S_CLKC : cmd[i] = 0x30; ++i; break;	// 读取实时脉冲数
            case S_ENCL : cmd[i] = 0x31; ++i; break;	// 读取经过线性化校准后的编码器值
            case S_CLKI : cmd[i] = 0x32; ++i; break;	// 读取输入脉冲数
            case S_TPOS : cmd[i] = 0x33; ++i; break;	// 读取电机目标位置
            case S_SPOS : cmd[i] = 0x34; ++i; break;	// 读取电机实时设定的目标位置
            case S_VEL  : cmd[i] = 0x35; ++i; break;	// 读取电机实时转速
            case S_CPOS : cmd[i] = 0x36; ++i; break;	// 读取电机实时位置
            case S_PERR : cmd[i] = 0x37; ++i; break;	// 读取电机位置误差
            case S_VBAT : cmd[i] = 0x38; ++i; break;	// 读取多圈编码器电池电压（Y42）
            case S_TEMP : cmd[i] = 0x39; ++i; break;	// 读取电机实时温度（X42S/Y42）
            case S_FLAG : cmd[i] = 0x3A; ++i; break;	// 读取电机状态标志位
            case S_OFLAG: cmd[i] = 0x3B; ++i; break;	// 读取回零状态标志位
            case S_OAF  : cmd[i] = 0x3C; ++i; break;	// 读取电机状态标志位 + 回零状态标志位（X42S/Y42）
            case S_PIN  : cmd[i] = 0x3D; ++i; break;	// 读取引脚IO状态（X42S/Y42）
            default: break;
        }
        
        cmd[i] = (uint8_t)(time_ms >> 8);  ++i;	// 定时时间
        cmd[i] = (uint8_t)(time_ms >> 0);  ++i;

        cmd[i] = 0x6B; ++i;                   	// 校验字节
        
        // 发送命令
        can_SendCmd(this->hcan, cmd, i);
        }


};



class TaskMotors
{
    public:
    DM4310_MultiPos synbeltMotor;            //同步带电机
    DM4310 gantryMotor;  
    DM8009P yawMotor;       


    ZDTStepper stringMotorL;
    ZDTStepper stringMotorR;

    ServoMotors triggerMotor;       //扳机电机


    TaskMotors(){};

    //gantry相关变量
    float gantry_max_spd;

    float synbelt_max_spd;
    /**
     * @brief 电机初始化函数，注册电机并设置初始状态
     * 
     */
    void MotorsInit()
    {

        // DMMotorHandler::Instance()->registerMotor(&this->gantryMotor, &hfdcan1, 0x01);
        // this->gantryMotor.controlMode = DMMotor::POS_SPD_MODE;
        // this->gantryMotor.torqueSet = 0.0f;
        // DMMotorHandler::Instance()->EnableMotor_Block(&this->gantryMotor);
        // this->gantry_max_spd = 4.0f;


        DMMotorHandler::Instance()->registerMotor(&this->yawMotor, &hfdcan2, 0x03);
        this->yawMotor.controlMode = DMMotor::SPD_MODE;
        this->yawMotor.torqueSet = 0.0f;
        this->synbeltMotor.speedSet = 0.0f;
        DMMotorHandler::Instance()->EnableMotor_Block(&this->yawMotor);
        
        DMMotorHandler::Instance()->registerMotor(&this->synbeltMotor, &hfdcan1, 0x02);
        this->synbeltMotor.controlMode = DMMotor::SPD_MODE;
        this->synbeltMotor.torqueSet = 0.0f;
        // this->synbelt_max_spd = 3.0f;
        this->synbeltMotor.speedSet = 20.0f;
        DMMotorHandler::Instance()->EnableMotor_Block(&this->synbeltMotor);
        this->synbeltMotor.positionSet = this->synbeltMotor.motorFeedback.positionFdb;
        DMMotorHandler::Instance()->sendControlData();


        this->triggerMotor.Init(&htim1, TIM_CHANNEL_3);

        this->stringMotorL.Init(&hfdcan3, 2, 0);
        this->stringMotorR.Init(&hfdcan3, 1, 1); //?这个positive_dir是什么来着


    }   


};


#endif // TASK_MOTOR_HPP
