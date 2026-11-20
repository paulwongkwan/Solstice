# Publisher Metadata
set(SOLSTICE_PUBLISHER_NAME "Third Party Publisher"
        CACHE STRING "The name of the publisher (not developer) of the application.")
set(SOLSTICE_PUBLISHER_WEBSITE ""
        CACHE STRING "The URL of the publisher's website.")
set(SOLSTICE_PUBLISHER_ISSUE_URL "https://github.com/paulwongkwan/Sunshine/issues"
        CACHE STRING "The URL of the publisher's support site or issue tracker.
        If you provide a modified version of Solstice, we kindly request that you use your own url.")

option(BUILD_DOCS "Build documentation" ON)
option(BUILD_TESTS "Build tests" ON)
option(NPM_OFFLINE "Use offline npm packages. You must ensure packages are in your npm cache." OFF)

option(BUILD_WERROR "Enable -Werror flag." OFF)

option(SOLSTICE_ENABLE_TRAY "Enable system tray icon." ON)

option(SOLSTICE_USE_STATIC_QT
        "Require static Qt libraries and their static third-party dependencies." ON)

option(BOOST_USE_STATIC "Use static boost libraries." ON)
