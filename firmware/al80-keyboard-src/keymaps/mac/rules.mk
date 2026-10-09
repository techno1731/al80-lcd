VIA_ENABLE = yes
VIAL_ENABLE = yes
VIALRGB_ENABLE = yes
LTO_ENABLE = yes
# The knob is handled by encoder_update_user() in keymap.c.
ENCODER_MAP_ENABLE = no

TAP_DANCE_ENABLE = no
COMBO_ENABLE = no
KEY_OVERRIDE_ENABLE = no

# Fn/Globe rides in the reserved byte of the 6-key report, which NKRO does not have.
APPLE_FN_ENABLE = yes
NKRO_ENABLE = no
OPT_DEFS += -DAPPLE_FN_ENABLE
