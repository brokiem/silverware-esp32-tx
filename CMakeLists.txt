cmake_minimum_required(VERSION 3.13)

# Disable remote component manager to ensure fast, offline, deterministic builds
set(ENV{IDF_COMPONENT_MANAGER} "0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(silverware_tx)
