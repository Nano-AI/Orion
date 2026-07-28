# The SwiftUI shell.
#
# Built with swiftc directly rather than through an Xcode project, so the whole
# tree stays one CMake build. The app links the engine's static library through
# the C facade in Bridge.h — no C++ crosses into Swift.

find_program(SWIFTC swiftc REQUIRED)

set(APP_BUNDLE   ${CMAKE_BINARY_DIR}/Orion.app)
set(APP_CONTENTS ${APP_BUNDLE}/Contents)
set(APP_BINARY   ${APP_CONTENTS}/MacOS/Orion)

set(SWIFT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/OrionApp.swift
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine.swift
    ${CMAKE_CURRENT_SOURCE_DIR}/ImageCanvas.swift
    ${CMAKE_CURRENT_SOURCE_DIR}/Viewport.swift
    ${CMAKE_CURRENT_SOURCE_DIR}/CanvasLayout.swift
    ${CMAKE_CURRENT_SOURCE_DIR}/Navigator.swift
    ${CMAKE_CURRENT_SOURCE_DIR}/TargetedAdjust.swift
    ${CMAKE_CURRENT_SOURCE_DIR}/ColorLoupe.swift
    ${CMAKE_CURRENT_SOURCE_DIR}/Histogram.swift
    ${CMAKE_CURRENT_SOURCE_DIR}/EditHistory.swift
    ${CMAKE_CURRENT_SOURCE_DIR}/ExportPanel.swift
    ${CMAKE_CURRENT_SOURCE_DIR}/CropOverlay.swift
    ${CMAKE_CURRENT_SOURCE_DIR}/Sidecar.swift
    ${CMAKE_CURRENT_SOURCE_DIR}/Library.swift
    ${CMAKE_CURRENT_SOURCE_DIR}/Filmstrip.swift
    ${CMAKE_CURRENT_SOURCE_DIR}/CompareOverlay.swift
    ${CMAKE_CURRENT_SOURCE_DIR}/Screenshot.swift
    ${CMAKE_CURRENT_SOURCE_DIR}/CurveEditor.swift
    ${CMAKE_CURRENT_SOURCE_DIR}/CurveMath.swift
    ${CMAKE_CURRENT_SOURCE_DIR}/CurveSupport.swift
    ${CMAKE_CURRENT_SOURCE_DIR}/AdjustmentSlider.swift
    ${CMAKE_CURRENT_SOURCE_DIR}/AdjustmentMath.swift
)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
               ${CMAKE_CURRENT_BINARY_DIR}/Info.plist @ONLY)

add_custom_command(
    OUTPUT ${APP_BINARY}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${APP_CONTENTS}/MacOS
    COMMAND ${CMAKE_COMMAND} -E make_directory ${APP_CONTENTS}/Resources
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_BINARY_DIR}/Info.plist ${APP_CONTENTS}/Info.plist
    COMMAND ${SWIFTC}
            -swift-version 5
            -target arm64-apple-macos14.0
            -O
            -import-objc-header ${CMAKE_CURRENT_SOURCE_DIR}/Bridge.h
            -I ${CMAKE_SOURCE_DIR}/engine/include
            ${SWIFT_SOURCES}
            -o ${APP_BINARY}
            -Xlinker -force_load -Xlinker $<TARGET_FILE:orion_engine>
            -Xlinker -rpath -Xlinker /opt/homebrew/opt/libraw/lib
            ${LIBRAW_LIBRARY}
            # The engine is C++ and throws internally, so the Swift link needs
            # libc++ and its unwinder even though nothing escapes the facade.
            -lc++ -lc++abi
            -framework Metal -framework MetalKit -framework Foundation
            -framework CoreGraphics -framework ImageIO -framework AppKit
    DEPENDS ${SWIFT_SOURCES}
            ${CMAKE_CURRENT_SOURCE_DIR}/Bridge.h
            ${CMAKE_CURRENT_BINARY_DIR}/Info.plist
            orion_engine
    COMMENT "swiftc    Orion.app"
    VERBATIM
)

add_custom_target(orion_app ALL DEPENDS ${APP_BINARY})
add_dependencies(orion_app orion_shaders)

# Viewport geometry tests. A separate tiny executable rather than a test
# framework — the point is that the maths gets checked at all.
set(VIEWPORT_TESTS ${CMAKE_BINARY_DIR}/orion-viewport-tests)

add_custom_command(
    OUTPUT ${VIEWPORT_TESTS}
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/ViewportTestsMain.swift
            ${CMAKE_CURRENT_BINARY_DIR}/main.swift
    COMMAND ${SWIFTC}
            -swift-version 5
            -target arm64-apple-macos14.0
            ${CMAKE_CURRENT_SOURCE_DIR}/Viewport.swift
            ${CMAKE_CURRENT_SOURCE_DIR}/CanvasLayout.swift
            ${CMAKE_CURRENT_SOURCE_DIR}/CurveMath.swift
            ${CMAKE_CURRENT_SOURCE_DIR}/CurveSupport.swift
            ${CMAKE_CURRENT_SOURCE_DIR}/TargetedAdjust.swift
            ${CMAKE_CURRENT_SOURCE_DIR}/AdjustmentMath.swift
            ${CMAKE_CURRENT_SOURCE_DIR}/EditHistory.swift
            ${CMAKE_CURRENT_SOURCE_DIR}/ViewportTests.swift
            ${CMAKE_CURRENT_BINARY_DIR}/main.swift
            -o ${VIEWPORT_TESTS}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Viewport.swift
            ${CMAKE_CURRENT_SOURCE_DIR}/CanvasLayout.swift
            ${CMAKE_CURRENT_SOURCE_DIR}/CurveMath.swift
            ${CMAKE_CURRENT_SOURCE_DIR}/CurveSupport.swift
            ${CMAKE_CURRENT_SOURCE_DIR}/TargetedAdjust.swift
            ${CMAKE_CURRENT_SOURCE_DIR}/AdjustmentMath.swift
            ${CMAKE_CURRENT_SOURCE_DIR}/EditHistory.swift
            ${CMAKE_CURRENT_SOURCE_DIR}/ViewportTests.swift
            ${CMAKE_CURRENT_SOURCE_DIR}/ViewportTestsMain.swift
    COMMENT "swiftc    orion-viewport-tests"
    VERBATIM
)

add_custom_target(orion_viewport_tests ALL DEPENDS ${VIEWPORT_TESTS})
