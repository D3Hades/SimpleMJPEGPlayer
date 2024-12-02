cmake_minimum_required(VERSION 3.15)
project(SimpleMJPEGPlayer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SimpleMJPEGPlayer "SimpleMJPEGPlayer.cpp" "SimpleMJPEGPlayer.h")

if(WIN32)
    set(SDL2_DIR "C:/libs/SDL2")

    set(SDL2_INCLUDE_DIR "${SDL2_DIR}/include")
    set(SDL2_LIBRARY "${SDL2_DIR}/lib/x64/SDL2.lib")
    set(SDL2_MAIN_LIB "${SDL2_DIR}/lib/x64/SDL2main.lib")

    set(SDL2_IMAGE_DIR "C:/libs/SDL2/SDL_Image")
    set(SDL2_IMAGE_INCLUDE_DIR "${SDL2_IMAGE_DIR}/include")
    set(SDL2_IMAGE_LIBRARY "${SDL2_IMAGE_DIR}/lib/x64/SDL2_image.lib")

    add_custom_command(TARGET SimpleMJPEGPlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SDL2_DIR}/lib/x64/SDL2.dll"
        $<TARGET_FILE_DIR:SimpleMJPEGPlayer>)

    add_custom_command(TARGET SimpleMJPEGPlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SDL2_IMAGE_DIR}/lib/x64/SDL2_image.dll"
        $<TARGET_FILE_DIR:SimpleMJPEGPlayer>)

    target_include_directories(SimpleMJPEGPlayer PRIVATE ${SDL2_INCLUDE_DIR} ${SDL2_IMAGE_INCLUDE_DIR})
    target_link_libraries(SimpleMJPEGPlayer PRIVATE ${SDL2_LIBRARY} ${SDL2_MAIN_LIB} ${SDL2_IMAGE_LIBRARY})
else()
    find_package(SDL2 REQUIRED)
    include_directories(headers ${SDL2_INCLUDE_DIRS})
    include_directories(SimpleMJPEGPlayer ${SDL2_INCLUDE_DIRS})
    target_link_libraries(SimpleMJPEGPlayer ${SDL2_LIBRARIES} SDL2_image)
endif()