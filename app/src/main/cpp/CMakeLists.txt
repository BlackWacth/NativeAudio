cmake_minimum_required(VERSION 3.22.1)

project("native-audio-jni")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99 -Wall")

add_library(
        ${PROJECT_NAME}
        SHARED
        native-audio-jni.c)

target_link_libraries(
        ${PROJECT_NAME}
        android
        log
        OpenSLES)