idf_component_register(
    SRCS "main.c" "board.c" "leds.c" "sounds.c" "store.c" "portal.c" "dns.c" "link.c" "ota.c" "improv.c" "status.c" "mww.cc"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "portal.html"
    EMBED_FILES "wake.tflite")
