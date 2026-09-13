# ENet 1.3.18 computes three header offsets through a null pointer. Use the
# standard offsetof in a generated translation unit, leaving fetched sources intact.
file(READ "${enet_SOURCE_DIR}/protocol.c" enet_protocol)
set(enet_old_offset "(size_t) & ((ENetProtocolHeader *) 0) -> sentTime")
string(REPLACE "${enet_old_offset}" "offsetof(ENetProtocolHeader, sentTime)"
       enet_protocol_fixed "${enet_protocol}")
if(enet_protocol_fixed STREQUAL enet_protocol)
  message(FATAL_ERROR "ENet protocol offset fix no longer matches; review the pinned source.")
endif()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(enet_fixed_source "${CMAKE_CURRENT_BINARY_DIR}/generated/enet-protocol.c")
file(CONFIGURE OUTPUT "${enet_fixed_source}"
     CONTENT "#include <stddef.h>\n${enet_protocol_fixed}" @ONLY)
get_target_property(enet_sources enet SOURCES)
list(REMOVE_ITEM enet_sources protocol.c)
set_property(TARGET enet PROPERTY SOURCES "${enet_sources};${enet_fixed_source}")
unset(enet_protocol)
unset(enet_protocol_fixed)
