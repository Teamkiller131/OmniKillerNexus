cmake_minimum_required(VERSION 3.20)

# Super OmniKiller — a Mario-like side-scroller that exercises the engine broadly:
# Jolt character controller + walking enemies + the CONTACT-EVENTS API (stomp vs.
# hurt), coin pickups, multi-texture disk sprites (stb), a side-scrolling camera,
# lives/score, and audio. Windows-only (sokol DX11), guarded on sokol + stb.

okn_add_sokol_game(NAME mario LIBS okn-audio okn-physics okn-input NEEDS_STB)
