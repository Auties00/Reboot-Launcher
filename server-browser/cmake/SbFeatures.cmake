# Probes the C++26 features the code can use and exposes them as SB_HAS_* definitions on sb_options.
include(CheckCXXSourceCompiles)

add_library(sb_options INTERFACE)
target_compile_features(sb_options INTERFACE cxx_std_26)

set(CMAKE_REQUIRED_FLAGS "-std=c++26 -freflection")
check_cxx_source_compiles([[
  #include <meta>
  struct S { int a; long b; };
  consteval auto n() { return std::meta::nonstatic_data_members_of(^^S, std::meta::access_context::unchecked()).size(); }
  int main() {
    S s{1, 2};
    long sum = 0;
    template for (constexpr auto m : std::define_static_array(std::meta::nonstatic_data_members_of(^^S, std::meta::access_context::unchecked())))
      sum += s.[:m:];
    static_assert(n() == 2);
    return int(sum) - 3;
  }
]] SB_HAS_REFLECTION)
unset(CMAKE_REQUIRED_FLAGS)
if(SB_HAS_REFLECTION)
  target_compile_options(sb_options INTERFACE -freflection)
  target_compile_definitions(sb_options INTERFACE SB_HAS_REFLECTION=1)
endif()

set(CMAKE_REQUIRED_FLAGS "-std=c++26 -fcontracts")
check_cxx_source_compiles([[
  int f(int x) pre(x > 0) { contract_assert(x != 2); return x; }
  int main() { return f(1) - 1; }
]] SB_HAS_CONTRACTS)
unset(CMAKE_REQUIRED_FLAGS)
if(SB_HAS_CONTRACTS)
  target_compile_options(sb_options INTERFACE -fcontracts)
  target_compile_definitions(sb_options INTERFACE SB_HAS_CONTRACTS=1)
endif()

message(STATUS "server-browser: reflection=${SB_HAS_REFLECTION} contracts=${SB_HAS_CONTRACTS}")
