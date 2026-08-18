if(NOT DEFINED STAGE_DIR)
    message(FATAL_ERROR "STAGE_DIR is required")
endif()
if(NOT DEFINED VERSION)
    message(FATAL_ERROR "VERSION is required")
endif()
if(NOT DEFINED PRODUCT_NAME)
    message(FATAL_ERROR "PRODUCT_NAME is required")
endif()
if(NOT DEFINED SLUG)
    message(FATAL_ERROR "SLUG is required")
endif()
if(NOT DEFINED BUNDLE_ID)
    message(FATAL_ERROR "BUNDLE_ID is required")
endif()
if(NOT DEFINED PLUGIN_CODE)
    message(FATAL_ERROR "PLUGIN_CODE is required")
endif()

file(WRITE "${STAGE_DIR}/ARTIFACTS.txt"
"Product: ${PRODUCT_NAME}
Version: ${VERSION}
Slug: ${SLUG}
Bundle ID: ${BUNDLE_ID}
Plugin Code: ${PLUGIN_CODE}
Staged artifact contract:
- standalone/${SLUG}_standalone_plugin.app or .exe
- vst3/${SLUG}_vst3_plugin.vst3
- au/${SLUG}_au_plugin.component on Apple
")
