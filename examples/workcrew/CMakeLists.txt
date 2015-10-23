SET(APP_NAME workcrew)

INCLUDE_DIRECTORIES(${PROJECT_BINARY_DIR}/include)

SET(APP_SRC
	ThreadObserver.cpp
	ThreadObserver.h
	ThreadReporter.cpp
	ThreadReporter.h
	WorkCrew.cpp
)

ADD_EXECUTABLE(${APP_NAME} ${APP_SRC})

TARGET_LINK_LIBRARIES(${APP_NAME} OpenThreads)
