cmake_minimum_required(VERSION 3.20)

# Knockdown — an Angry-Birds-style physics game on the OmniKillerNexus engine.
# Reuses okn-render's sokol_gfx sprite renderer + D3D11 sokol app TU and okn-audio,
# and drives gameplay with okn-physics (Jolt backend) — including the contact-events
# API (IPhysicsWorld::drain_contacts) for impact sounds + scoring. Windows-only
# (sokol DX11), guarded on sokol.

okn_add_sokol_game(NAME knockdown LIBS okn-audio okn-physics)
