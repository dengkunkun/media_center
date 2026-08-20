cmake_minimum_required(VERSION 3.20)
project(streamer)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-std=c++20 -Wall -Wextra -Wpedantic)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
# uncomment the following section in order to fill in
# further dependencies manually.
# find_package(<dependency> REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nav2_msgs REQUIRED)
find_package(nav2_util REQUIRED)
find_package(tf2_ros REQUIRED)

set(dependencies
  rclcpp
  rclcpp_action
  geometry_msgs
  nav2_msgs
  nav2_util
  tf2_ros
)

find_library(
  LIBAVUTIL
  NAMES avutil
  HINTS /usr/local/lib
  REQUIRED
)
find_library(
  LIBAVFILTER
  NAMES avfilter
  HINTS /usr/local/lib
  REQUIRED
)
find_library(
  LIBAVFORMAT
  NAMES avformat
  HINTS /usr/local/lib
  REQUIRED
)
find_library(
  LIBAVCODEC
  NAMES avcodec
  HINTS /usr/local/lib
  REQUIRED
)
find_library(
  LIBAVDEVICE
  NAMES avdevice
  HINTS /usr/local/lib
  REQUIRED
)

include_directories(/usr/include/pipewire-0.3)
include_directories(/usr/include/spa-0.2)


add_executable(streamer src/main.cpp src/streamer_node.cpp
	src/client.cpp
	src/dispatch_queue.cpp
	src/stream.cpp
	src/stream_handler.cpp
  src/data_transceiver.cpp
  src/cmd_vel_telemetry.cpp
  src/pose_uploader.cpp
)

# target_link_libraries(streamer PUBLIC rclcpp::rclcpp
	# m datachannel pipewire-0.3 ${LIBAVUTIL} ${LIBAVFILTER} ${LIBAVFORMAT} ${LIBAVCODEC} ${LIBAVDEVICE})
ament_target_dependencies(streamer PUBLIC ${dependencies})
target_link_libraries(streamer PUBLIC m datachannel pipewire-0.3 ${LIBAVUTIL} ${LIBAVFILTER} ${LIBAVFORMAT} ${LIBAVCODEC} ${LIBAVDEVICE})

install(TARGETS
  streamer
  DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  # the following line skips the linter which checks for copyrights
  # comment the line when a copyright and license is added to all source files
  set(ament_cmake_copyright_FOUND TRUE)
  # the following line skips cpplint (only works in a git repo)
  # comment the line when this package is in a git repo and when
  # a copyright and license is added to all source files
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
