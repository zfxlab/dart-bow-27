# Select PNX's USBX application adapter without modifying CubeMX-generated files.
# Call after adding the CubeMX subdirectory, before adding product sources.
# Inputs: PROJECT_NAME, ENABLE_USBX, PNX_EMBEDDED_GENERATED_DIR.
# Only this adapter edits the generated USB target sources/libraries.
get_target_property(pnx_mx_sources ${PROJECT_NAME} SOURCES)
list(FILTER pnx_mx_sources EXCLUDE REGEX "/USBX/App/(app_usbx_device|ux_device_cdc_acm)\\.c$")
if(NOT ENABLE_USBX)
    list(FILTER pnx_mx_sources EXCLUDE REGEX "/USBX/App/ux_device_descriptors\\.c$")
    get_target_property(pnx_mx_libraries ${PROJECT_NAME} LINK_LIBRARIES)
    list(REMOVE_ITEM pnx_mx_libraries USBX)
    set_property(TARGET ${PROJECT_NAME} PROPERTY LINK_LIBRARIES "${pnx_mx_libraries}")
    if(TARGET USBX)
        set_property(TARGET USBX PROPERTY EXCLUDE_FROM_ALL TRUE)
    endif()
    # CubeMX calls this hook unconditionally. A disabled product has no USBX
    # stack/controller startup; the public USB backend remains unselected.
    set(pnx_disabled_usbx "${PNX_EMBEDDED_GENERATED_DIR}/usbx_disabled.c")
    string(CONCAT pnx_disabled_usbx_content
        "/* Generated product startup hook: USBX is not selected. */\n"
        "#include \"tx_api.h\"\n"
        "UINT MX_USBX_Device_Init(VOID* memory_ptr) { (void)memory_ptr; return TX_SUCCESS; }\n")
    file(CONFIGURE OUTPUT "${pnx_disabled_usbx}" CONTENT "${pnx_disabled_usbx_content}" @ONLY)
    list(APPEND pnx_mx_sources "${pnx_disabled_usbx}")
endif()
set_property(TARGET ${PROJECT_NAME} PROPERTY SOURCES "${pnx_mx_sources}")
