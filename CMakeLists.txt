cmake_minimum_required(VERSION 3.16)

project(disk-space-analyzer VERSION 1.0.0 LANGUAGES C CXX)
set(APP_VERSION "${PROJECT_VERSION}-beta3")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)

# ---- web UI files compiled into the payload
# url path | MIME type | source
set(WEB_ASSETS
  "/index.html|text/html|web/index.html"
  "/app.css|text/css|web/app.css"
  "/app.js|application/javascript|web/app.js"
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
  "}\n")
configure_file(${GEN}/web_assets_table.cpp.in ${GEN}/web_assets_table.cpp COPYONLY)

# ---- the payload
add_executable(disk-space-analyzer
  src/main.cpp
  src/api.cpp
  src/games.cpp
  src/http.cpp
  src/launcher.cpp
  src/platform.cpp
  src/system.cpp
  src/tree.cpp
  ${GEN}/web_assets_table.cpp
  ${ASSET_SOURCES}
)
target_include_directories(disk-space-analyzer PRIVATE src)
target_compile_definitions(disk-space-analyzer PRIVATE APP_VERSION="${APP_VERSION}")
target_compile_options(disk-space-analyzer PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(disk-space-analyzer PRIVATE Threads::Threads)

if(PS5)
  set_target_properties(disk-space-analyzer PROPERTIES SUFFIX ".elf")
  # libSceAppInstUtil needs libSceIpmi loaded first: linked alone (or loaded
  # with sceKernelLoadStartModule) the payload hangs before main() and never
  # logs anything. Same set and order as ps5-web-file-manager.
  target_link_libraries(disk-space-analyzer PRIVATE
    SceIpmi SceAppInstUtil SceUserService SceSystemService kernel_sys)
endif()
