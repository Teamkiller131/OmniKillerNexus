cmake_minimum_required(VERSION 3.20)

# NETBOX — the netcode's first game consumer (headless console exe, like swarm).
# Server-authoritative state-sync: an okn-ecs World replicated tick-by-tick as
# okn-network Snapshot deltas over a ReliabilityLayer on the testkit FaultyLink
# (deterministic drops + reordering), verified by blob + state_hash equality.

add_executable(netbox "${CMAKE_CURRENT_SOURCE_DIR}/netbox_app.cpp")
target_link_libraries(netbox PRIVATE okn-ecs okn-network)
target_compile_features(netbox PRIVATE cxx_std_23)
