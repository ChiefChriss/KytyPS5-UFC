set(KYTY_GIT_VERSION "unknown")
set(KYTY_GIT_HASH "unknown")
set(KYTY_GIT_REVISION "unknown")
if(GIT_EXECUTABLE)
	execute_process(
		COMMAND "${GIT_EXECUTABLE}" describe --tags --always --dirty
		WORKING_DIRECTORY "${GIT_WORKING_DIRECTORY}"
		OUTPUT_VARIABLE KYTY_GIT_VERSION
		OUTPUT_STRIP_TRAILING_WHITESPACE
		RESULT_VARIABLE GIT_RESULT
		ERROR_QUIET
	)
	if(NOT GIT_RESULT EQUAL 0)
		set(KYTY_GIT_VERSION "unknown")
	endif()

	execute_process(
		COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
		WORKING_DIRECTORY "${GIT_WORKING_DIRECTORY}"
		OUTPUT_VARIABLE KYTY_GIT_REVISION
		OUTPUT_STRIP_TRAILING_WHITESPACE
		RESULT_VARIABLE GIT_HASH_RESULT
		ERROR_QUIET
	)
	if(NOT GIT_HASH_RESULT EQUAL 0)
		set(KYTY_GIT_HASH "unknown")
		set(KYTY_GIT_REVISION "unknown")
	else()
		string(SUBSTRING "${KYTY_GIT_REVISION}" 0 7 KYTY_GIT_HASH)
		execute_process(
			COMMAND "${GIT_EXECUTABLE}" diff-index --quiet HEAD --
			WORKING_DIRECTORY "${GIT_WORKING_DIRECTORY}"
			RESULT_VARIABLE GIT_DIRTY_RESULT
			ERROR_QUIET
		)
		if(NOT GIT_DIRTY_RESULT EQUAL 0)
			string(APPEND KYTY_GIT_HASH "-dirty")
		endif()
	endif()
endif()

# Local build counter, bumped by Build-Windows.ps1 on every build.
set(KYTY_BUILD_ITERATION "")
if(EXISTS "${GIT_WORKING_DIRECTORY}/build-iteration.txt")
	file(STRINGS "${GIT_WORKING_DIRECTORY}/build-iteration.txt" KYTY_BUILD_ITERATION
	     LIMIT_COUNT 1 REGEX "^[0-9]+$")
endif()
if(KYTY_BUILD_ITERATION)
	math(EXPR KYTY_BUILD_ITERATION "${KYTY_BUILD_ITERATION}")
	string(LENGTH "${KYTY_BUILD_ITERATION}" iteration_length)
	if(iteration_length LESS 3)
		math(EXPR padding "3 - ${iteration_length}")
		string(REPEAT "0" ${padding} zeros)
		set(KYTY_BUILD_ITERATION "${zeros}${KYTY_BUILD_ITERATION}")
	endif()
	set(KYTY_BUILD_ITERATION_LABEL "UFC Dev ${KYTY_BUILD_ITERATION} | ")
else()
	set(KYTY_BUILD_ITERATION_LABEL "")
endif()
configure_file("${INPUT_FILE}" "${OUTPUT_FILE}")
