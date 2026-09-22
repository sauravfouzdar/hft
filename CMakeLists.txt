cmake_minimum_required(VERSION 3.28)
project(hft LANGUAGES CXX)

# --- C++23, no compiler extensions ----------------------------------------
# Standard is strict: we want to know if we lean on a GNU extension by accident.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Emit compile_commands.json so editors / clang-tidy see the exact flags.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Toolchain: Homebrew LLVM ---------------------------------------------
# We build with upstream clang (>= 18) from Homebrew, not Apple clang. It is
# the only option on this Mac that gives BOTH working sanitizers (ASan/UBSan/
# TSan) AND a C++23 standard library new enough for std::print etc. It is
# keg-only, so it is not on the default PATH and its libc++ must be found
# explicitly at link AND run time -- otherwise we'd silently fall back to the
# older system libc++. Configure with:
#   -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++
set(HFT_LLVM_PREFIX "/opt/homebrew/opt/llvm" CACHE PATH "Homebrew LLVM prefix")
add_link_options("-L${HFT_LLVM_PREFIX}/lib/c++"
                 "-Wl,-rpath,${HFT_LLVM_PREFIX}/lib/c++")

# With -flto the compiler emits LLVM bitcode into .o/.a files. Apple's ar/ranlib
# cannot build a valid symbol index for a bitcode archive -- the link then dies
# with "archive member '/' not a mach-o file". Use LLVM's ar/ranlib, which
# understand bitcode, for any static library built with LTO.
find_program(HFT_LLVM_AR     llvm-ar     REQUIRED HINTS "${HFT_LLVM_PREFIX}/bin")
find_program(HFT_LLVM_RANLIB llvm-ranlib REQUIRED HINTS "${HFT_LLVM_PREFIX}/bin")
set(CMAKE_AR     "${HFT_LLVM_AR}"     CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB "${HFT_LLVM_RANLIB}" CACHE FILEPATH "" FORCE)
# Setting CMAKE_AR alone is not enough: CMake bakes the archive *creation*
# command at language-init time and it kept /usr/bin/ar. Define the archive
# commands explicitly so the bitcode-aware llvm-ar runs for create AND append.
set(CMAKE_CXX_ARCHIVE_CREATE "${HFT_LLVM_AR} qc <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_CXX_ARCHIVE_APPEND "${HFT_LLVM_AR} q  <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_CXX_ARCHIVE_FINISH "${HFT_LLVM_RANLIB} <TARGET>")

# Default to Release. The hot path is the point; the optimized build is the
# baseline, and the sanitizer build is the deliberate exception (see below).
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Release Asan Tsan Debug)

# --- Release: the config the hot path will run under ----------------------
# -O3        full optimization
# -march=native  use every instruction this CPU has (verified to work on this
#                arm64 target with GCC 15; on x86 it is the usual spelling)
# -flto      optimize across translation units, so a hot loop that calls into
#            another .cpp can still be inlined
set(CMAKE_CXX_FLAGS_RELEASE        "-O3 -march=native -flto -DNDEBUG")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-O3 -march=native -flto")

# --- Asan: the config tests run under -------------------------------------
# Address + UB sanitizer. -O1 keeps it usable, -g gives readable reports,
# -fno-omit-frame-pointer gives honest stack traces. We never benchmark this
# build: sanitizers add instrumentation that would swamp the latencies we care
# about. Correctness here, latency in Release.
set(CMAKE_CXX_FLAGS_ASAN        "-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined")
set(CMAKE_EXE_LINKER_FLAGS_ASAN "-fsanitize=address,undefined")

# --- Tsan: the config the concurrency tests run under ---------------------
# ThreadSanitizer detects data races -- the real judge of lock-free code. It
# cannot be combined with ASan, so it is its own build type / build dir. Same
# rule as Asan: never benchmark it.
set(CMAKE_CXX_FLAGS_TSAN        "-O1 -g -fno-omit-frame-pointer -fsanitize=thread")
set(CMAKE_EXE_LINKER_FLAGS_TSAN "-fsanitize=thread")

add_executable(hello app/hello/main.cpp)

# Bake the active configuration name into the binary so `hello` can report
# which build produced it. $<CONFIG> resolves per build directory.
target_compile_definitions(hello PRIVATE HFT_BUILD_CONFIG="$<CONFIG>")

# --- core library ---------------------------------------------------------
# Shared engine primitives. Headers are included as "core/<name>.hpp", so the
# include root is the project root.
add_library(core STATIC core/tsc_clock.cpp)
target_include_directories(core PUBLIC ${CMAKE_SOURCE_DIR})

# --- telemetry library ----------------------------------------------------
# Measurement primitives (latency histogram, etc.).
add_library(telemetry STATIC telemetry/latency_hist.cpp)
target_include_directories(telemetry PUBLIC ${CMAKE_SOURCE_DIR})

# --- tests ----------------------------------------------------------------
# Built and run under whichever config the build dir uses; we run them under
# the Asan build so every test pass is also a sanitizer pass.
option(HFT_BUILD_TESTS "Build the unit tests" ON)
if(HFT_BUILD_TESTS)
  find_package(GTest CONFIG REQUIRED)
  enable_testing()

  add_executable(test_tsc_clock tests/test_tsc_clock.cpp)
  target_link_libraries(test_tsc_clock PRIVATE core GTest::gtest_main)
  add_test(NAME tsc_clock COMMAND test_tsc_clock)

  add_executable(test_latency_hist tests/test_latency_hist.cpp)
  target_link_libraries(test_latency_hist PRIVATE telemetry GTest::gtest_main)
  add_test(NAME latency_hist COMMAND test_latency_hist)

  # core/types is header-only; the test just needs the include root.
  add_executable(test_types tests/test_types.cpp)
  target_include_directories(test_types PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(test_types PRIVATE GTest::gtest_main)
  add_test(NAME types COMMAND test_types)

  # core/mutex_queue is header-only (template).
  add_executable(test_mutex_queue tests/test_mutex_queue.cpp)
  target_include_directories(test_mutex_queue PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(test_mutex_queue PRIVATE GTest::gtest_main)
  add_test(NAME mutex_queue COMMAND test_mutex_queue)

  # core/spsc_ring is header-only (template). Run under Tsan for the race check.
  add_executable(test_spsc_ring tests/test_spsc_ring.cpp)
  target_include_directories(test_spsc_ring PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(test_spsc_ring PRIVATE GTest::gtest_main)
  add_test(NAME spsc_ring COMMAND test_spsc_ring)
endif()

# --- benchmarks -----------------------------------------------------------
# Run these ONLY from the Release build dir; sanitizer instrumentation would
# make the numbers meaningless. (CLAUDE.md: never benchmark the Asan build.)
option(HFT_BUILD_BENCHMARKS "Build the benchmarks" ON)
if(HFT_BUILD_BENCHMARKS)
  find_package(benchmark CONFIG REQUIRED)

  add_executable(bench_mutex_queue benchmarks/bench_mutex_queue.cpp)
  target_include_directories(bench_mutex_queue PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(bench_mutex_queue PRIVATE benchmark::benchmark)

  # 1.4: mutex queue vs lock-free ring, head to head in one executable so the
  # two rows are directly comparable.
  add_executable(bench_ring_vs_mutex benchmarks/bench_ring_vs_mutex.cpp)
  target_include_directories(bench_ring_vs_mutex PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(bench_ring_vs_mutex PRIVATE benchmark::benchmark)

  # 1.5: false-sharing experiment -- packed vs padded counters, same ring logic.
  add_executable(bench_false_sharing benchmarks/bench_false_sharing.cpp)
  target_include_directories(bench_false_sharing PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(bench_false_sharing PRIVATE benchmark::benchmark)
endif()
