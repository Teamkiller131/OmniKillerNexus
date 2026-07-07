cmake_minimum_required(VERSION 3.20)

# Flappy Bird — a game built on the OmniKillerNexus engine. Reuses okn-render's
# sokol_gfx sprite renderer + D3D11 sokol app TU, okn-audio for sound, and the
# header-only 2D sprite path. Windows-only (sokol DX11), guarded on sokol.

okn_add_sokol_game(NAME flappy LIBS okn-audio)
