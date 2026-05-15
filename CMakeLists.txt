cmake_minimum_required(VERSION 3.16)

# esp32-camera 컴포넌트 경로 (idf_component_manager 사용 시 자동 처리)
# 수동 설정 시: set(EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/../esp32-camera)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32s3_ov2640)