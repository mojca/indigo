SET(INDIGO_VERSION "1.1.4")

IF(BUILD_VERSION)
   SET(INDIGO_BUILD_VERSION ${BUILD_VERSION})
ELSE()
   SET(INDIGO_BUILD_VERSION 0)
ENDIF()

SET(INDIGO_VERSION_EXT "${INDIGO_VERSION}.${INDIGO_BUILD_VERSION} ${PACKAGE_SUFFIX}")
