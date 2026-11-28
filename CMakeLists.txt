cmake_minimum_required(VERSION 3.16)

project(disk-space-analyzer VERSION 1.0.0 LANGUAGES C CXX)
# -DAPP_VERSION=... overrides it, e.g. to try the update on an older version.
if(NOT APP_VERSION)
  set(APP_VERSION "${PROJECT_VERSION}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)

# ---- web UI files compiled into the payload
# url path | MIME type | source
set(WEB_ASSETS
  "/index.html|text/html|web/index.html"
  "/app.css|text/css|web/app.css"
  "/app.js|application/javascript|web/app.js"
  "/report.html|text/html|web/report.html"
  "/icon0.png|image/png|assets/icon.png"
  "/manifest.webmanifest|application/manifest+json|web/manifest.webmanifest"
  "/fonts/Lato-Regular.ttf|font/ttf|assets/fonts/LatoLatin-Regular.ttf"
  "/fonts/Lato-Bold.ttf|font/ttf|assets/fonts/LatoLatin-Bold.ttf"
)

set(GEN ${CMAKE_CURRENT_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${GEN})
set(ASSET_SOURCES)
set(ASSET_DECLS "")
set(ASSET_ROWS "")
set(i 0)
foreach(entry ${WEB_ASSETS})
  string(REPLACE "|" ";" parts "${entry}")
  list(GET parts 0 url)
  list(GET parts 1 mime)
  list(GET parts 2 src)
  set(sym asset_${i})
  set(out ${GEN}/${sym}.c)
  add_custom_command(OUTPUT ${out}
    COMMAND ${CMAKE_COMMAND} -DIN=${CMAKE_CURRENT_SOURCE_DIR}/${src} -DOUT=${out} -DSYM=${sym}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${src} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
    COMMENT "Embedding ${src}")
  list(APPEND ASSET_SOURCES ${out})
  string(APPEND ASSET_DECLS "extern const unsigned char ${sym}[];\nextern const size_t ${sym}_len;\n")
  string(APPEND ASSET_ROWS "  {\"${url}\", \"${mime}\", ${sym}, 0},\n")
  math(EXPR i "${i} + 1")
endforeach()
file(WRITE ${GEN}/web_assets_table.cpp.in
  "#include <cstring>\n#include \"web_assets.h\"\n"
  "extern \"C\" {\n${ASSET_DECLS}}\n"
  "static Asset g_assets[] = {\n${ASSET_ROWS}};\n"
  "static const size_t* g_lens[] = {\n")
set(i 0)
foreach(entry ${WEB_ASSETS})
  file(APPEND ${GEN}/web_assets_table.cpp.in "  &asset_${i}_len,\n")
  math(EXPR i "${i} + 1")
endforeach()
file(APPEND ${GEN}/web_assets_table.cpp.in
  "};\n"
  "const Asset* FindAsset(const char* path) {\n"
  "  for (size_t i = 0; i < sizeof g_assets / sizeof g_assets[0]; i++) {\n"
  "    g_assets[i].size = *g_lens[i];\n"
  "    if (!std::strcmp(g_assets[i].path, path)) return &g_assets[i];\n"
  "  }\n"
  "  return nullptr;\n"
  "}\n"
  "const Asset* AssetAt(size_t i) {\n"
  "  if (i >= sizeof g_assets / sizeof g_assets[0]) return nullptr;\n"
  "  g_assets[i].size = *g_lens[i];\n"
  "  return &g_assets[i];\n"
  "}\n")
configure_file(${GEN}/web_assets_table.cpp.in ${GEN}/web_assets_table.cpp COPYONLY)

# ---- the payload
add_executable(disk-space-analyzer
  src/main.cpp
  src/api.cpp
  src/browser_layout.cpp
  src/games.cpp
  src/http.cpp
  src/launcher.cpp
  src/platform.cpp
  src/report.cpp
  src/settings.cpp
  src/system.cpp
  src/tree.cpp
  src/uninstall.cpp
  src/update.cpp
  ${GEN}/web_assets_table.cpp
  ${ASSET_SOURCES}
)
target_include_directories(disk-space-analyzer PRIVATE src)
target_compile_definitions(disk-space-analyzer PRIVATE APP_VERSION="${APP_VERSION}")
target_compile_options(disk-space-analyzer PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(disk-space-analyzer PRIVATE Threads::Threads)

if(PS5)
  # ---- browser full screen: vendor/browser-fullscreen, copied and patched
  # for running inside this payload (patches/browser-fullscreen-embed.patch).
  set(FS_SRC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/browser-fullscreen/src)
  set(FS_PATCH ${CMAKE_CURRENT_SOURCE_DIR}/patches/browser-fullscreen-embed.patch)
  set(FS_OUT ${CMAKE_CURRENT_BINARY_DIR}/browser-fullscreen)
  if(NOT EXISTS ${FS_SRC}/main.c)
    message(FATAL_ERROR "vendor/browser-fullscreen is empty: run git submodule update --init")
  endif()
  file(GLOB FS_FILES ${FS_SRC}/*.c ${FS_SRC}/*.h)
  add_custom_command(OUTPUT ${FS_OUT}/main.c ${FS_OUT}/hbc_patch.c
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${FS_OUT}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${FS_OUT}
    COMMAND ${CMAKE_COMMAND} -E copy ${FS_FILES} ${FS_OUT}
    COMMAND patch -s -p1 -d ${FS_OUT} -i ${FS_PATCH}
    DEPENDS ${FS_FILES} ${FS_PATCH}
    COMMENT "Preparing browser-fullscreen")
  target_sources(disk-space-analyzer PRIVATE src/browser_patch.c ${FS_OUT}/hbc_patch.c)
  set_source_files_properties(src/browser_patch.c PROPERTIES
    INCLUDE_DIRECTORIES ${FS_OUT} OBJECT_DEPENDS ${FS_OUT}/main.c COMPILE_OPTIONS "-w")
  set_source_files_properties(${FS_OUT}/hbc_patch.c PROPERTIES COMPILE_OPTIONS "-w")

  set_target_properties(disk-space-analyzer PROPERTIES SUFFIX ".elf")
  # libSceAppInstUtil needs libSceIpmi loaded first: linked alone (or loaded
  # with sceKernelLoadStartModule) the payload hangs before main() and never
  # logs anything. Same set and order as ps5-web-file-manager.
  target_link_libraries(disk-space-analyzer PRIVATE
    SceIpmi SceAppInstUtil SceUserService SceSystemService kernel_sys)

  # ---- HTTPS for the update check: the toolchain's libcurl and OpenSSL,
  # with its CA list built in (the console has none a payload can use).
  set(HB ${PS5_PAYLOAD_SDK}/target/user/homebrew)
  if(NOT EXISTS ${HB}/lib/libcurl.a OR NOT EXISTS ${HB}/etc/ca-bundle.crt)
    message(FATAL_ERROR "libcurl not found in ${HB}: run make toolchain")
  endif()
  add_custom_command(OUTPUT ${GEN}/ca_bundle.c
    COMMAND ${CMAKE_COMMAND} -DIN=${HB}/etc/ca-bundle.crt -DOUT=${GEN}/ca_bundle.c -DSYM=ca_bundle
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
    DEPENDS ${HB}/etc/ca-bundle.crt ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
    COMMENT "Embedding ca-bundle.crt")
  target_sources(disk-space-analyzer PRIVATE ${GEN}/ca_bundle.c)
  target_include_directories(disk-space-analyzer PRIVATE ${HB}/include)
  target_link_libraries(disk-space-analyzer PRIVATE
    ${HB}/lib/libcurl.a ${HB}/lib/libpsl.a ${HB}/lib/libssl.a ${HB}/lib/libcrypto.a
    ${HB}/lib/libzstd.a ${HB}/lib/libz.a)
else()
  find_package(CURL REQUIRED)
  target_link_libraries(disk-space-analyzer PRIVATE CURL::libcurl)
endif()
