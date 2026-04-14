idf_component_register(
    SRCS
        "sparkplug_node.c"
        "sparkplug_payloads.c"
        "sparkplug_decode.c"
        "sparkplug_session.c"
        "vendor/eclipse_tahu/sparkplug_b.pb.c"
        "vendor/nanopb/pb_common.c"
        "vendor/nanopb/pb_encode.c"
        "vendor/nanopb/pb_decode.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS
        "."
        "vendor/eclipse_tahu"
        "vendor/nanopb"
    EMBED_TXTFILES "certs/ca-chain.cert.pem"
    REQUIRES app_config esp_timer mqtt sensor_tmp36 time_sync wifi_manager
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE PB_FIELD_32BIT=1)
