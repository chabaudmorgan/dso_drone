SET(PROJECT_NAME DSO)

PROJECT(${PROJECT_NAME})
CMAKE_MINIMUM_REQUIRED(VERSION 3.1.0)
#set(CMAKE_VERBOSE_MAKEFILE ON)



#set(BUILD_TYPE Release)
set(BUILD_TYPE RelWithDebInfo)

set(EXECUTABLE_OUTPUT_PATH bin)
set(LIBRARY_OUTPUT_PATH lib)
set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

# required libraries
find_package(SuiteParse REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(Boost COMPONENTS system thread) 
find_package(raspicam REQUIRED)

# optional libraries
find_package(LibZip QUIET)
find_package(Pangolin 0.2 QUIET)
find_package(OpenCV QUIET)
find_package(Threads QUIET)

# flags
set(CMAKE_CXX_FLAGS
   "-O3 -std=c++0x"
)

# ARM compilation with neon (for Raspberry Pi 3)
if(${CMAKE_SYSTEM_PROCESSOR} MATCHES ".*arm.*")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mfloat-abi=hard -mfpu=neon-fp-armv8")
	# -march=native induces errors on Raspberry Pi 3
else()
	add_definitions("-DENABLE_SSE")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SSE_FLAGS} -march=native")
endif()

if(MSVC)
     set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")
endif(MSVC)

message("--- Compiling options: ${CMAKE_CXX_FLAGS}")


# Sources files
set(dso_SOURCE_FILES
  ${PROJECT_SOURCE_DIR}/src/FullSystem/FullSystem.cpp
  ${PROJECT_SOURCE_DIR}/src/FullSystem/FullSystemOptimize.cpp
  ${PROJECT_SOURCE_DIR}/src/FullSystem/FullSystemOptPoint.cpp
  ${PROJECT_SOURCE_DIR}/src/FullSystem/FullSystemDebugStuff.cpp
  ${PROJECT_SOURCE_DIR}/src/FullSystem/FullSystemMarginalize.cpp
  ${PROJECT_SOURCE_DIR}/src/FullSystem/Residuals.cpp
  ${PROJECT_SOURCE_DIR}/src/FullSystem/CoarseTracker.cpp
  ${PROJECT_SOURCE_DIR}/src/FullSystem/CoarseInitializer.cpp
  ${PROJECT_SOURCE_DIR}/src/FullSystem/ImmaturePoint.cpp
  ${PROJECT_SOURCE_DIR}/src/FullSystem/HessianBlocks.cpp
  ${PROJECT_SOURCE_DIR}/src/FullSystem/PixelSelector2.cpp
  ${PROJECT_SOURCE_DIR}/src/OptimizationBackend/EnergyFunctional.cpp
  ${PROJECT_SOURCE_DIR}/src/OptimizationBackend/AccumulatedTopHessian.cpp
  ${PROJECT_SOURCE_DIR}/src/OptimizationBackend/AccumulatedSCHessian.cpp
  ${PROJECT_SOURCE_DIR}/src/OptimizationBackend/EnergyFunctionalStructs.cpp
  ${PROJECT_SOURCE_DIR}/src/util/settings.cpp
  ${PROJECT_SOURCE_DIR}/src/util/Undistort.cpp
  ${PROJECT_SOURCE_DIR}/src/util/globalCalib.cpp

  ${PROJECT_SOURCE_DIR}/src/IOWrapper/InputDepthWrapper.cpp
)



include_directories(
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/thirdparty/Sophus
  ${PROJECT_SOURCE_DIR}/thirdparty/sse2neon
  ${EIGEN3_INCLUDE_DIR}
) 


# decide if we have pangolin
if(Pangolin_FOUND)
	message("--- Found PANGOLIN, compiling dso_pangolin library.")
	include_directories( ${Pangolin_INCLUDE_DIRS} ) 
	set(dso_pangolin_SOURCE_FILES 
	  ${PROJECT_SOURCE_DIR}/src/IOWrapper/Pangolin/KeyFrameDisplay.cpp
	  ${PROJECT_SOURCE_DIR}/src/IOWrapper/Pangolin/PangolinDSOViewer.cpp)
	set(HAS_PANGOLIN 1)
else()
	message("--- Could not find PANGOLIN, not compiling dso_pangolin library.")
	message("    This means there will be no 3D display / GUI available for dso_dataset.")
	set(dso_pangolin_SOURCE_FILES )
	set(HAS_PANGOLIN 0)
endif()

# decide if we have openCV
if(OpenCV_FOUND)
	message("--- Found OpenCV, compiling dso_opencv library.")
	include_directories( ${OpenCV_INCLUDE_DIRS} )
	set(dso_opencv_SOURCE_FILES 
	  ${PROJECT_SOURCE_DIR}/src/IOWrapper/OpenCV/ImageDisplay_OpenCV.cpp
	  ${PROJECT_SOURCE_DIR}/src/IOWrapper/OpenCV/ImageRW_OpenCV.cpp)
	set(HAS_OPENCV 1)
else()
	message("--- Could not find OpenCV, not compiling dso_opencv library.")
	message("    This means there will be no image display, and image read / load functionality.")
	set(dso_opencv_SOURCE_FILES 
	  ${PROJECT_SOURCE_DIR}/src/IOWrapper/ImageDisplay_dummy.cpp
	  ${PROJECT_SOURCE_DIR}/src/IOWrapper/ImageRW_dummy.cpp)
	set(HAS_OPENCV 0)
endif()

# decide if we have ziplib.
if(LIBZIP_LIBRARY)
	message("--- Found ziplib (${LIBZIP_VERSION}), compiling with zip capability.")
	add_definitions(-DHAS_ZIPLIB=1)
	include_directories( ${LIBZIP_INCLUDE_DIR_ZIP} ${LIBZIP_INCLUDE_DIR_ZIPCONF} ) 
else()
	message("--- Not found ziplib (${LIBZIP_LIBRARY}), compiling without zip capability.")
	set(LIBZIP_LIBRARY "")
endif()

# decide if we have raspicam
if (raspicam_CV_FOUND)
	message("--- Found raspicam.")
	include_directories( ${raspicam_CV_INCLUDE_DIRS} )
else ()
	message("--- Not found raspicam (${raspicam_CV_LIBRARY}).")
	set(raspicam_CV_LIBRARY "")
endif ()

# compile main library.
include_directories( ${CSPARSE_INCLUDE_DIR} ${CHOLMOD_INCLUDE_DIR}) 
add_library(dso ${dso_SOURCE_FILES} ${dso_opencv_SOURCE_FILES} ${dso_pangolin_SOURCE_FILES})

#set_property( TARGET dso APPEND_STRING PROPERTY COMPILE_FLAGS -Wall )


# build original dso_dataset (might be deleted at some point) (only if we have both OpenCV and Pangolin)
#if (OpenCV_FOUND AND Pangolin_FOUND)
#	message("--- compiling dso_dataset.")
#	add_executable(dso_dataset ${PROJECT_SOURCE_DIR}/src/main_dso_pangolin.cpp )
#    	target_link_libraries(dso_dataset dso boost_system boost_thread cxsparse ${LIBZIP_LIBRARY} ${Pangolin_LIBRARIES} ${OpenCV_LIBS})
#else()
#	message("--- not building dso_dataset, since either don't have openCV or Pangolin.")
#endif()

# Build dso_drone
if(OpenCV_FOUND AND Pangolin_FOUND AND raspicam_CV_FOUND)
	message("--- Compiling dso_drone.")
	add_executable(dso_drone ${PROJECT_SOURCE_DIR}/src/main_drone.cpp)
	target_link_libraries(dso_drone dso boost_system boost_thread cxsparse ${LIBZIP_LIBRARY} ${Pangolin_LIBRARIES} ${OpenCV_LIBS} ${raspicam_CV_LIBS} ${CMAKE_THREAD_LIBS_INIT})
else()
	message("--- Not building dso_drone, since either don't have openCV or Pangolin or raspicam.")
endif()

