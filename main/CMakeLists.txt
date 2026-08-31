idf_component_register(
    SRCS
        "main.c"
        "src/energyboxx_api.c"
        "src/wifi_provisioning.c"
        "src/wifi_web.c"
        "src/dns_server.c"
        "src/wifi_storage.c"
        "src/api_storage.c"
        "src/status_view.c"
        "src/display.c"
        "src/uri_decode.c"
    INCLUDE_DIRS
        "."
        "inc"
    EMBED_FILES
        "../assets/energy-owl-bringup.bin"
    REQUIRES
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        esp_http_client
        esp_driver_gpio
        esp_http_server
        esp-tls
        mbedtls
        esp_timer
)

#  Warnings for this component only. ESP-IDF itself is built with a milder set,
#  and turning these on project-wide would bury our own output in warnings from
#  code we do not maintain.
#
#  Two of these would have caught bugs found by hand in this review:
#  -Wformat=2 flags both the %d-with-size_t of M2 and the non-literal format
#  string of C4, and -Wstrict-prototypes flags the missing void of L10.
#
#  ESP-IDF turns some of these off again further down its own command line, so
#  they are repeated here after its flags rather than set in the toolchain.

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Wshadow
    -Wformat=2
    -Wpointer-arith
    -Wvla
    -Wstrict-prototypes
    -Wmissing-prototypes
    -Wunused-parameter
)

#  Deliberately not enabled: -Wundef and -Wredundant-decls. Both only fire
#  inside ESP-IDF's own headers - assert.h tests an undefined config macro,
#  stdio.h declares flockfile twice - which we include but do not maintain.
#  A warning that can only be silenced by patching somebody else's header is
#  noise, and noise is what makes people stop reading warnings.

#  dns_server.c is Espressif's captive-portal example, taken verbatim with its
#  own licence header. Its warnings are not ours to fix, and patching a vendored
#  file makes the next update harder, so it keeps the milder default set.
set_source_files_properties(src/dns_server.c PROPERTIES COMPILE_OPTIONS
    "-Wno-shadow;-Wno-extra;-Wno-unused-parameter;-Wno-missing-prototypes")
