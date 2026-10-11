#pragma once
#include <pebble.h>

#define __TESTING 0

#if __TESTING == 1
#define LOG(...) app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) app_log(APP_LOG_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...) app_log(APP_LOG_LEVEL_INFO, __FILE__, __LINE__, __VA_ARGS__)
#else
#define LOG(...)
#define LOG_ERROR(...)
#define LOG_INFO(...)
#endif

#define BACKGROUND GColorBlack
#define FOREGROUND GColorWhite
// #define CURSOR PBL_IF_COLOR_ELSE(GColorDarkGray, GColorDarkGray)
#define HIGHLIGHT PBL_IF_COLOR_ELSE(GColorRed, GColorWhite)
#define HLFOREGROUND PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack)
#define INACTIVECELL PBL_IF_COLOR_ELSE(GColorDarkGray, GColorDarkGray)
#define CHARTCOLOR PBL_IF_COLOR_ELSE(GColorRed, GColorWhite)
#define INACTIVETEXT PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack)
#define ACTIVETEXT PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack)
