# Cờ biên dịch dùng chung cho code PAWBOUND (không áp lên dependency bên thứ ba).
function(pb_set_warnings target)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
      -Wcast-align -Woverloaded-virtual)
  elseif(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
  endif()
endfunction()

# MinGW: link tĩnh runtime để .exe chạy được mà không cần C:\mingw64\bin trong PATH.
if(MINGW)
  add_link_options(-static -static-libgcc -static-libstdc++)
endif()
