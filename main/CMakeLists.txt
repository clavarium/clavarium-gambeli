idf_component_register(
    SRCS "main.c" "board.c" "leds.c" "sounds.c" "store.c" "portal.c" "dns.c" "link.c" "ota.c" "improv.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "portal.html")
