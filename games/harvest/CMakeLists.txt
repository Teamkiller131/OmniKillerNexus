cmake_minimum_required(VERSION 3.20)

# OmniHarvest — a Stardew-Valley-like 2D TILE-GRID farming sim. A different-genre
# framework test: top-down, grid-based, and deliberately using NO physics — it links
# only okn-math + okn-audio + okn-input + the 2D GPU sprite path, proving that stack
# alone carries a whole game. Windows-only (sokol DX11), guarded on sokol + stb.

okn_add_sokol_game(NAME harvest LIBS okn-audio okn-input NEEDS_STB)
