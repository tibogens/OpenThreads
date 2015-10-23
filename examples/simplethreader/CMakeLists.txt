SET(APP_NAME simplethreader)

INCLUDE_DIRECTORIES(${PROJECT_BINARY_DIR}/include)

SET(APP_SRC
	SimpleThreader.cpp
	ThreadObserver.cpp
	ThreadObserver.h
	ThreadReporter.cpp
	ThreadReporter.h
)

ADD_EXECUTABLE(${APP_NAME} ${APP_SRC})

TARGET_LINK_LIBRARIES(${APP_NAME} OpenThreads)
