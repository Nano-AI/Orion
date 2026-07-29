# A tiny Swift measurement tool. Built with swiftc directly, like the app.
find_program(SWIFTC swiftc REQUIRED)

set(PIXSTAT ${CMAKE_BINARY_DIR}/orion-pixstat)

add_custom_command(
    OUTPUT ${PIXSTAT}
    COMMAND ${SWIFTC} -O -swift-version 5 -target arm64-apple-macos14.0
            ${CMAKE_CURRENT_SOURCE_DIR}/main.swift -o ${PIXSTAT}
            -framework Foundation -framework CoreGraphics -framework ImageIO
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/main.swift
    COMMENT "swiftc    orion-pixstat"
    VERBATIM
)

add_custom_target(orion_pixstat ALL DEPENDS ${PIXSTAT})
