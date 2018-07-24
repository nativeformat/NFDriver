
if(IOS)
  add_executable(NFDriverApp
    ${APP_TYPE}
    NFDriverCLI.cpp)
  set_target_properties(NFDriverApp
    PROPERTIES
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "iPhone Developer: My Name")
  target_link_libraries(NFDriverApp NFDriver)
elseif(ANDROID_APP)
  add_library(NFDriverApp SHARED NFDriverCLI.cpp)
  target_link_libraries(NFDriverApp
    log
    android
    OpenSLES)
else()
  add_executable(NFDriverCLI NFDriverCLI.cpp)
  target_link_libraries(NFDriverCLI NFDriver)
endif()
