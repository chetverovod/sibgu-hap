check_include_file_cxx(stdint.h HAVE_STDINT_H)
if(HAVE_STDINT_H)
    add_definitions(-DHAVE_STDINT_H)
endif()

set(examples_as_tests_sources)
if(${ENABLE_EXAMPLES})
    set(examples_as_tests_sources
        #test/sibgu-hap-examples-test-suite.cc
        )
endif()

build_lib(
    LIBNAME sibgu-hap
    SOURCE_FILES model/sibgu-hap.cc
                 helper/sibgu-hap-helper.cc
    HEADER_FILES model/sibgu-hap.h
                 helper/sibgu-hap-helper.h
    LIBRARIES_TO_LINK ${libcore}
    TEST_SOURCES test/sibgu-hap-test-suite.cc
                 ${examples_as_tests_sources}
)

