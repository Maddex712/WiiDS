cmake_minimum_required(VERSION 3.8)

add_library(JSystem INTERFACE)
target_include_directories(JSystem INTERFACE include/JSystem)

add_library(Dolphin INTERFACE)
target_include_directories(Dolphin INTERFACE include/Dolphin)

add_library(SMS INTERFACE)
target_include_directories(SMS INTERFACE include/SMS)

add_library(Kamek INTERFACE)
target_include_directories(Kamek INTERFACE include/Kamek)

add_library(Kuribo INTERFACE)
target_include_directories(Kuribo INTERFACE include/Kuribo)