#include "configuration.hpp"
#include "dart_config.hpp"

namespace dart
{
dart_state state{};
dart_state::dart_state() : config{}, runtime{}
{
    for (int id = 0; id <= 16; ++id) config.dart[id].id = id;
    for (int i = 0; i < 4; ++i) config.sequence[i] = i + 1;

    runtime.current_shot_number = 1;
    runtime.current_dart_id = config.sequence[0];
    runtime.current_aim_target = {0.0f, 0.0f};
    runtime.vision_door_status = door_state::closed;
    runtime.last_vision_door_status = door_state::closed;
    runtime.last_fire_finished = false;
    runtime.fired_count_this_open = 0;

    runtime.game_status_ladar = false;
    runtime.game_status_char = '\0';

    runtime.referee.game_status = 0;
    runtime.referee.shooting_remaining_time = 0;
    runtime.referee.chosen_target = 0;
    runtime.referee.launch_station_status = 1;
    runtime.referee.last_launch_station_status = 1;

    runtime.auto_aim.enable = false;
    runtime.auto_aim.yaw_ok = false;
    runtime.auto_aim.light_lost = false;
    runtime.auto_aim.running = false;
    runtime.auto_aim.autoaim_allow = false;
    runtime.auto_aim.last_autoaim_allow = false;
    runtime.auto_aim.referee_launch_closed_stable = false;
    runtime.auto_aim.referee_launch_open_stable = false;
    runtime.auto_aim.vision_door_closed_stable = false;
    runtime.auto_aim.vision_door_open_stable = false;
}

/**
 * @brief 结合visionrx数据与referee的发射台状态共同判断门的状态，更新门状态相关的runtime数据
 * 
 * @param rx 
 */
void dart_state::update_door(vision_rx* rx)
{
    if (rx->light_detected == 1 || rx->light_detected == 2) 
    {
        runtime.vision_door_status = door_state::open;
    }
    else if (rx->light_detected == 0 || rx->light_detected == 3 )
    {
        runtime.vision_door_status = door_state::closed;
    }

    //连续10tick视觉门状态为视为vision_door_open_stable = true
    if (runtime.vision_door_status != door_state::open)
    {
        runtime.auto_aim.vision_door_open_stable = false;
        runtime.auto_aim.vision_door_open_delay.reset();
    }
    else if (!runtime.auto_aim.vision_door_open_stable &&
             runtime.auto_aim.vision_door_open_delay.stable(true, cfg::system::vision_open_ticks))
    {
        runtime.auto_aim.vision_door_open_stable = true;
    }

    //连续700tick视觉门状态为关视为vision_door_closed_stable = true。700是测试过的值(防止飞镖飞过摄像头的误判）)
    if (runtime.vision_door_status != door_state::closed)
    {
        runtime.auto_aim.vision_door_closed_stable = false;
        runtime.auto_aim.vision_door_closed_delay.reset();
    }
    else if (!runtime.auto_aim.vision_door_closed_stable &&
             runtime.auto_aim.vision_door_closed_delay.stable(true, cfg::system::vision_closed_ticks))
    {
        runtime.auto_aim.vision_door_closed_stable = true;
    }

    //连续10tick裁判系统发射台开视为referee_launch_open_stable = true
    if (runtime.referee.launch_station_status != 0)
    {
        runtime.auto_aim.referee_launch_open_stable = false;
        runtime.auto_aim.referee_launch_open_delay.reset();
    }
    else if (!runtime.auto_aim.referee_launch_open_stable &&
             runtime.auto_aim.referee_launch_open_delay.stable(true, cfg::system::referee_stable_ticks))
    {
        runtime.auto_aim.referee_launch_open_stable = true;
    }

    //连续10tick裁判系统发射台关视为referee_launch_closed_stable = true
    if (runtime.referee.launch_station_status != 1)
    {
        runtime.auto_aim.referee_launch_closed_stable = false;
        runtime.auto_aim.referee_launch_closed_delay.reset();
    }
    else if (!runtime.auto_aim.referee_launch_closed_stable &&
             runtime.auto_aim.referee_launch_closed_delay.stable(true, cfg::system::referee_stable_ticks))
    {
        runtime.auto_aim.referee_launch_closed_stable = true;
    }
}

void dart_state::update_prepare()
{
    //视觉门开 or 裁判系统发射台开，视为门开
    if (runtime.auto_aim.door_open_delay.stable(runtime.auto_aim.vision_door_open_stable || runtime.auto_aim.referee_launch_open_stable, cfg::system::door_stable_ticks))
    {
        if (!runtime.auto_aim.autoaim_allow)
        {
            runtime.fired_count_this_open = 0;
        }
        runtime.auto_aim.autoaim_allow = true;
    }
    //视觉门关 and 裁判系统发射台关，视为门关
    if (runtime.auto_aim.door_closed_delay.stable(runtime.auto_aim.referee_launch_closed_stable && runtime.auto_aim.vision_door_closed_stable, cfg::system::door_stable_ticks))
    {
        runtime.auto_aim.autoaim_allow = false;
    }



}
/**
 * @brief 创建或更新飞镖距离表，表中每个点包含了对应距离的yaw_offset和tension_kg数据
 * 
 * @param table 
 * @param table_len 
 */
void dart_state::set_table(const table_point* table, uint16_t table_len)
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
 * @return aim_point 
 */
aim_point dart_state::lookup_aim(int id, float distance) const
{
    int dart_id = (id >= 1 && id <= 16) ? id : 0;
    aim_point aim = {config.dart[dart_id].yaw_offset, config.dart[dart_id].tension_kg_base};
    if (dart_id == 0 || distance <= 0.0f || config.base_distance_table_len == 0)
    {
        return aim;
    }

    const table_point* lower = nullptr;
    const table_point* upper = nullptr;

    for (uint16_t i = 0; i < config.base_distance_table_len; i++)
    {
        const table_point* point = &config.base_distance_table[i];
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
 * @return slot 
 */
slot dart_state::prepare_slot() const
{
    switch (runtime.current_shot_number)
    {
        case 1:
            return slot::none;
        case 2:
            return slot::slot_1;
        case 3:
            return slot::slot_2;
        case 4:
            return slot::slot_3;
        default:
            return slot::none;
    }
}

/**
 * @brief 更新current_dart_id
 * 
 */
void dart_state::update_dart_id()
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
void dart_state::update_shot(launcher_status* msg)
{
    if (msg->is_fire_finished && !runtime.last_fire_finished)
    {
        runtime.current_shot_number++;
        update_dart_id();
    }

}

/**
 * @brief 更新已发射状态
 * 
 * @param msg 
 */
void dart_state::update_fired(launcher_status* msg)
{
    if (msg->is_fire_finished && !runtime.last_fire_finished)
    {
        runtime.fired_count_this_open++;
    }


}

void dart_state::update_history(launcher_status* msg)
{
    runtime.last_vision_door_status = runtime.vision_door_status;
    runtime.last_fire_finished = msg->is_fire_finished;
    runtime.auto_aim.last_autoaim_allow = runtime.auto_aim.autoaim_allow;
}

} // namespace dart
