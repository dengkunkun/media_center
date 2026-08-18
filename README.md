# media_center

机器人ROS2媒体模块。

---

## 初始化ROS2环境
```
source /opt/ros/lyrical/setup.bash
```

## 创建模块
```
cd src
ros2 pkg create --build-type ament_cmake --license Apache-2.0 streamer
```

## 编译模块
```
colcon build --packages-select streamer --mixin compile-commands
```

## 安装模块
```
source install/setup.bash
```


---


## 运行语音对讲
```
ros2 run streamer streamer
```


## 运行语音播放
```
ros2 run speaker speaker
```


## 客户端调用播放，暂停，继续，停止
```
ros2 run speaker play_audio_client play [path] [volume] [loop]
ros2 run speaker play_audio_client pause [session]
ros2 run speaker play_audio_client resume [session]
ros2 run speaker play_audio_client stop [session]
```


## 设置系统音量
注意，要**浮点数**！
```
ros2 param set speaker_node volume 80.0
ros2 param get speaker_node volume
```

