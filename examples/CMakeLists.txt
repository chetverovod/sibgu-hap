build_lib_example(
    NAME sibgu-hap-example
    SOURCE_FILES sibgu-hap-example.cc
    LIBRARIES_TO_LINK ${libsibgu-hap}                     
)
 
build_lib_example(
    NAME wifi-simple-hap
    SOURCE_FILES wifi-simple-hap.cc
    LIBRARIES_TO_LINK  ${libsibgu-hap}
                       ${libinternet}
                       ${libwifi}
                       ${libflow-monitor}
)

build_lib_example(
    NAME wifi-simple-hap-router
    SOURCE_FILES wifi-simple-hap-router.cc
    LIBRARIES_TO_LINK  ${libsibgu-hap}
                       ${libinternet}
                       ${libwifi}
                       ${libflow-monitor}
)

build_lib_example(
    NAME wifi-moving-hap-router
    SOURCE_FILES wifi-moving-hap-router.cc
    LIBRARIES_TO_LINK  ${libsibgu-hap}
                       ${libinternet}
                       ${libwifi}
                       ${libflow-monitor}
                       ${libnetanim}
)

build_lib_example(
    NAME hap-sat-hap-simple
    SOURCE_FILES hap-sat-hap-simple.cc
    LIBRARIES_TO_LINK  ${libsibgu-hap}
                       ${libinternet}
                       ${libwifi}
                       ${libflow-monitor}
                       ${libnetanim}
)

build_lib_example(
    NAME hap-sat-hap-simple_2
    SOURCE_FILES hap-sat-hap-simple_2.cc
    LIBRARIES_TO_LINK  ${libsibgu-hap}
                       ${libinternet}
                       ${libwifi}
                       ${libflow-monitor}
                       ${libnetanim}
)


build_lib_example(
    NAME hap-sat-hap
    SOURCE_FILES hap-sat-hap.cc
    LIBRARIES_TO_LINK  ${libsibgu-hap}
                       ${libinternet}
                       ${libwifi}
                       ${libsatellite}  
                       ${libflow-monitor}
                       ${libnetanim}
)

