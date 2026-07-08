cmake_minimum_required(VERSION 3.20)

# Swarm — the headless ECS scale/perf gate (not a sokol game; a console exe like
# voidborne's autodemo). 20k entities through the parallel scheduler, sequential
# vs work-stealing pool, asserting scheduling-independent state hashes + a loose
# frame budget. Runs everywhere the engine builds — no window, no GPU.

add_executable(swarm "${CMAKE_CURRENT_SOURCE_DIR}/swarm_app.cpp")
target_link_libraries(swarm PRIVATE okn-ecs okn-platform)
target_compile_features(swarm PRIVATE cxx_std_23)
