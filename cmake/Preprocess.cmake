if(NOT DEFINED HW_COMPILER OR NOT DEFINED HW_MODE OR
   NOT DEFINED HW_INPUT OR NOT DEFINED HW_OUTPUT)
    message(FATAL_ERROR "Preprocess.cmake requires HW_COMPILER, HW_MODE, HW_INPUT, and HW_OUTPUT")
endif()

get_filename_component(_hw_output_directory "${HW_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_hw_output_directory}")

if(HW_MODE STREQUAL "MSVC")
    execute_process(
        COMMAND "${HW_COMPILER}" /nologo /EP /TC "${HW_INPUT}"
        OUTPUT_FILE "${HW_OUTPUT}"
        ERROR_VARIABLE _hw_error
        RESULT_VARIABLE _hw_result)
else()
    execute_process(
        COMMAND "${HW_COMPILER}" -E -x c "${HW_INPUT}"
        OUTPUT_FILE "${HW_OUTPUT}"
        ERROR_VARIABLE _hw_error
        RESULT_VARIABLE _hw_result)
endif()

if(NOT _hw_result EQUAL 0)
    file(REMOVE "${HW_OUTPUT}")
    message(FATAL_ERROR "Preprocessing ${HW_INPUT} failed:\n${_hw_error}")
endif()
