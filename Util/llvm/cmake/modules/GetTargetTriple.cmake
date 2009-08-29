# Returns the host triple.
# Invokes config.guess

function( get_target_triple var )
  if( MSVC )
    if( CMAKE_CL_64 )
      set( ${var} "x86_64-pc-win32" PARENT_SCOPE )
    else()
      set( ${var} "i686-pc-win32" PARENT_SCOPE )
    endif()
  elseif( MINGW AND NOT MSYS )
    set( ${var} "i686-pc-mingw32" PARENT_SCOPE )
  else( MSVC )
    set(config_guess ${LLVM_MAIN_SRC_DIR}/autoconf/config.guess)
    execute_process(COMMAND sh ${config_guess}
      RESULT_VARIABLE TT_RV
      OUTPUT_VARIABLE TT_OUT
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    if( NOT TT_RV EQUAL 0 )
      message(FATAL_ERROR "Failed to execute ${config_guess}")
    endif( NOT TT_RV EQUAL 0 )
    set( ${var} ${TT_OUT} PARENT_SCOPE )
    message(STATUS "Target triple: ${${var}}")
  endif( MSVC )
endfunction( get_target_triple var )
