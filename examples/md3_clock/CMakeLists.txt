cmake_minimum_required(VERSION 3.18)
project(MD3Clock LANGUAGES CXX)
# Installed SDK consumer: no paths to Qimgui implementation or gallery.
find_package(QimguiMD3 0.1 REQUIRED CONFIG COMPONENTS MD3 AndroidOverlay)
add_executable(MD3Clock src/main.cpp src/app.cpp src/model.cpp src/pages.cpp)
target_compile_features(MD3Clock PRIVATE cxx_std_17)
target_link_libraries(MD3Clock PRIVATE Qimgui::MD3 Qimgui::AndroidOverlay)
