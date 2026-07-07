cmake_minimum_required(VERSION 3.20)

# Super OmniKiller 3D — a true-3D Mario-like on the engine's mesh3d renderer
# (perspective camera + depth buffer + lit boxes on sokol_gfx) and Jolt in full 3D.
# Windows-only (sokol DX11), guarded on sokol.

okn_add_sokol_game(NAME mario3d LIBS okn-audio okn-physics okn-input
                   RENDER_TU mesh3d/mesh_renderer.cpp)
