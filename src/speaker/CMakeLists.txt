cmake_minimum_required(VERSION 3.20)
project(speaker)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(media_interfaces REQUIRED)

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

include_directories(/usr/include/pipewire-0.3)
include_directories(/usr/include/spa-0.2)

add_executable(speaker src/speaker.cpp)

target_link_libraries(speaker PUBLIC rclcpp::rclcpp media_interfaces::media_interfaces
 asound pipewire-0.3 ${LIBAVUTIL} ${LIBAVFILTER} ${LIBAVFORMAT} ${LIBAVCODEC})
# ament_target_dependencies(speaker PUBLIC rclcpp media_interfaces)
# target_link_libraries(speaker PUBLIC asound ${LIBAVUTIL} ${LIBAVFILTER} ${LIBAVFORMAT} ${LIBAVCODEC})

add_executable(play_audio_client src/play_audio_client.cpp)


target_link_libraries(play_audio_client PUBLIC rclcpp::rclcpp media_interfaces::media_interfaces)
# ament_target_dependencies(play_audio_client PUBLIC rclcpp media_interfaces)



install(TARGETS
  speaker
  play_audio_client
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
