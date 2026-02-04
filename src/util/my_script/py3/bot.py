#  pip3 install pymavlink --break-system-packages 
from pymavutil import mavutil

# 连接至ROV
master = mavutil.mav_connection('udpin:0.0.0.0:14550')
# 等待心跳信息
master.wait_heartbeat()


"""
预位与停机例子
"""

# 预位
master.mav.command_long_send(
    master.target_system,
    master.target_component,
    mavutil.mav.MAV_CMD_COMPONENT_ARM_DISARM,
    0,
    1, 0, 0, 0, 0, 0, 0)

print("Waiting for the vehicle to arm")
master.motors_armed_wait()
print('Armed!')

# 停机
master.mav.command_long_send(
    master.target_system,
    master.target_component,
    mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM,
    0, 
    0, 0, 0, 0, 0, 0, 0)

master.motors_disarmed_wait()



"""
更改控制模式例子
"""
mode = 'STABILIZE'

# 检测控制模式 
if mode not in master.mode_mapping():
    print('Unknown mode : {}'.format(mode))
    print('Try:', list(master.mode_mapping().keys()))
    sys.exit(1)

# 检测模式ID
mode_id = master.mode_mapping()[mode]
# 设置新模式
master.mav.set_mode_send(
    master.target_system,
    mavutil.mavlink.MAV_MODE_FLAG_CUSTOM_MODE_ENABLED,
    mode_id)

while True:
    ack_msg = master.recv_match(type='COMMAND_ACK', blocking=True)
    ack_msg = ack_msg.to_dict()

    #等待设置完成
    if ack_msg['command'] != mavutil.mavlink.MAV_CMD_DO_SET_MODE:
        continue

        # 打印控制模式
    print(mavutil.mavlink.enums['MAV_RESULT'][ack_msg['result']].description)
    break


"""
手动控制例子
"""


master.mav.control_send(
    master.target_system,
    500,			# X方向控制 [-1000,1000]
    -500,	# Y方向控制 [-1000,1000]	
    250,		# Z方向控制 [0,1000]
    500,		# 翻滚控制 [-1000,1000]
    0)         # 按钮信息 

# 机器人不动，1号，4号，8号按钮被按下
buttons = 1 + 1 << 3 + 1 << 7
master.mav.control_send(
    master.target_system,
    0,
    0,
    500, 
    0,
    buttons)



"""
通道控制例子
"""
def set_channel_pwm(channel_id, pwm=1500):
        #channel_id (TYPE): 通道 ID
        #pwm (int, optional): pwm 值 1100-1900

    #检测通道
    if channel_id < 1 or channel_id > 18:
        print("Channel does not exist.")
        return

    rc_channel_values = [65535 for _ in range(18)]
    rc_channel_values[channel_id - 1] = pwm
    master.mav.rc_channels_override_send(
master.target_system,                        
master.target_component,                    
*rc_channel_values)                  


# 2号通道控制，产生翻滚
set_channel_pwm(2, 1600)

# 4号通道控制，产生转向
set_channel_pwm(4, 1600)

# 摄像头舵机控制
set_channel_pwm(8, 1900)

