set(CMAKE_CXX_COMPILER x86_64-apple-$ENV{OSXCROSS_TARGET}-clang++)
set(CMAKE_C_COMPILER x86_64-apple-$ENV{OSXCROSS_TARGET}-clang)
set(CMAKE_OSX_SYSROOT $ENV{OSXCROSS_SDK})
set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -stdlib=libc++")
