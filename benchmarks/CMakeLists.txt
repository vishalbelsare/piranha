# Setup for the split compilation of benchmarks.

# NOTE: this needs to be a macro since it is modifying the external variable _PIRANHA_TEST_NUM.
macro(ADD_PIRANHA_BENCHMARK arg1)
	if(PIRANHA_TEST_NSPLIT)
		math(EXPR _PIRANHA_TEST_NUM "(${_PIRANHA_TEST_NUM} + 1) % ${PIRANHA_TEST_NSPLIT}")
	endif()
	if(NOT PIRANHA_TEST_NSPLIT OR "${PIRANHA_TEST_SPLIT_NUM}" STREQUAL "${_PIRANHA_TEST_NUM}")
		add_executable("${arg1}" "${arg1}.cpp")
		target_compile_options(${arg1} PRIVATE "$<$<CONFIG:DEBUG>:${PIRANHA_CXX_FLAGS_DEBUG}>" "$<$<CONFIG:RELEASE>:${PIRANHA_CXX_FLAGS_RELEASE}>")
		# Pass the current source dir as a definition, so we can load data for tests from the source dir.
		target_compile_definitions(${arg1} PRIVATE PIRANHA_TESTS_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}")
		target_link_libraries(${arg1} piranha Boost::system Boost::filesystem)
		set_property(TARGET ${arg1} PROPERTY CXX_STANDARD 11)
		set_property(TARGET ${arg1} PROPERTY CXX_STANDARD_REQUIRED YES)
		set_property(TARGET ${arg1} PROPERTY CXX_EXTENSIONS NO)
	endif()
endmacro()

ADD_PIRANHA_BENCHMARK(audi)
ADD_PIRANHA_BENCHMARK(estimation)
ADD_PIRANHA_BENCHMARK(evaluate)
ADD_PIRANHA_BENCHMARK(fateman1)
ADD_PIRANHA_BENCHMARK(fateman1_dynamic)
ADD_PIRANHA_BENCHMARK(fateman1_rational)
ADD_PIRANHA_BENCHMARK(fateman1_unpacked)
ADD_PIRANHA_BENCHMARK(fateman1_unpacked_truncation)
ADD_PIRANHA_BENCHMARK(fateman2)
ADD_PIRANHA_BENCHMARK(gastineau1)
ADD_PIRANHA_BENCHMARK(gastineau2)
ADD_PIRANHA_BENCHMARK(gastineau3)
ADD_PIRANHA_BENCHMARK(gastineau4)
ADD_PIRANHA_BENCHMARK(memory_perf)
ADD_PIRANHA_BENCHMARK(monagan1)
ADD_PIRANHA_BENCHMARK(monagan2)
ADD_PIRANHA_BENCHMARK(monagan3)
ADD_PIRANHA_BENCHMARK(monagan4)
ADD_PIRANHA_BENCHMARK(monagan5)
ADD_PIRANHA_BENCHMARK(power_series)
ADD_PIRANHA_BENCHMARK(pearce1)
ADD_PIRANHA_BENCHMARK(pearce1_dynamic)
ADD_PIRANHA_BENCHMARK(pearce1_rational)
ADD_PIRANHA_BENCHMARK(pearce1_unpacked)
ADD_PIRANHA_BENCHMARK(pearce2)
ADD_PIRANHA_BENCHMARK(pearce2_unpacked)
if(PIRANHA_WITH_MSGPACK AND PIRANHA_WITH_BZIP2)
	ADD_PIRANHA_BENCHMARK(perminov1)
endif()
ADD_PIRANHA_BENCHMARK(rectangular)
ADD_PIRANHA_BENCHMARK(s11n_perf)
ADD_PIRANHA_BENCHMARK(symengine_expand2b)
