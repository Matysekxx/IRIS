#ifndef DATE_TIME_LIB_H
#define DATE_TIME_LIB_H

#include "core/Native.h"
#include "core/Value.h"
#include <ctime>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace iris::std_lib {

    inline double iris_date_time_now() {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        return static_cast<double>(ms);
    }

    inline std::tm iris_date_time_to_tm(double timestampMs) {
        time_t seconds = static_cast<time_t>(timestampMs / 1000.0);
#ifdef _WIN32
        std::tm result{};
        localtime_s(&result, &seconds);
        return result;
#else
        return *localtime(&seconds);
#endif
    }

    inline iris::core::Value iris_date_time_year(iris::core::Value* args, int argCount) {
        double ts = (argCount >= 1 && args[0].isDouble()) ? args[0].asDouble()
                   : (argCount >= 1 && args[0].isInt()) ? static_cast<double>(args[0].asInt())
                   : iris_date_time_now();
        auto t = iris_date_time_to_tm(ts);
        return iris::core::Value(t.tm_year + 1900);
    }

    inline iris::core::Value iris_date_time_month(iris::core::Value* args, int argCount) {
        double ts = (argCount >= 1 && args[0].isDouble()) ? args[0].asDouble()
                   : (argCount >= 1 && args[0].isInt()) ? static_cast<double>(args[0].asInt())
                   : iris_date_time_now();
        auto t = iris_date_time_to_tm(ts);
        return iris::core::Value(t.tm_mon + 1);
    }

    inline iris::core::Value iris_date_time_day(iris::core::Value* args, int argCount) {
        double ts = (argCount >= 1 && args[0].isDouble()) ? args[0].asDouble()
                   : (argCount >= 1 && args[0].isInt()) ? static_cast<double>(args[0].asInt())
                   : iris_date_time_now();
        auto t = iris_date_time_to_tm(ts);
        return iris::core::Value(t.tm_mday);
    }

    inline iris::core::Value iris_date_time_hour(iris::core::Value* args, int argCount) {
        double ts = (argCount >= 1 && args[0].isDouble()) ? args[0].asDouble()
                   : (argCount >= 1 && args[0].isInt()) ? static_cast<double>(args[0].asInt())
                   : iris_date_time_now();
        auto t = iris_date_time_to_tm(ts);
        return iris::core::Value(t.tm_hour);
    }

    inline iris::core::Value iris_date_time_minute(iris::core::Value* args, int argCount) {
        double ts = (argCount >= 1 && args[0].isDouble()) ? args[0].asDouble()
                   : (argCount >= 1 && args[0].isInt()) ? static_cast<double>(args[0].asInt())
                   : iris_date_time_now();
        auto t = iris_date_time_to_tm(ts);
        return iris::core::Value(t.tm_min);
    }

    inline iris::core::Value iris_date_time_second(iris::core::Value* args, int argCount) {
        double ts = (argCount >= 1 && args[0].isDouble()) ? args[0].asDouble()
                   : (argCount >= 1 && args[0].isInt()) ? static_cast<double>(args[0].asInt())
                   : iris_date_time_now();
        auto t = iris_date_time_to_tm(ts);
        return iris::core::Value(t.tm_sec);
    }

    inline iris::core::Value iris_date_time_format(iris::core::Value* args, int argCount) {
        if (argCount < 1 || !args[0].isString()) return iris::core::Value("");

        std::string fmt = args[0].str();
        double ts = (argCount >= 2 && args[1].isDouble()) ? args[1].asDouble()
                   : (argCount >= 2 && args[1].isInt()) ? static_cast<double>(args[1].asInt())
                   : iris_date_time_now();

        auto t = iris_date_time_to_tm(ts);

        std::ostringstream result;
        for (size_t i = 0; i < fmt.length(); i++) {
            if (fmt[i] == '%' && i + 1 < fmt.length()) {
                i++;
                switch (fmt[i]) {
                    case 'Y': result << std::setw(4) << (t.tm_year + 1900); break;
                    case 'y': result << std::setw(2) << std::setfill('0') << ((t.tm_year + 1900) % 100); break;
                    case 'm': result << std::setw(2) << std::setfill('0') << (t.tm_mon + 1); break;
                    case 'd': result << std::setw(2) << std::setfill('0') << t.tm_mday; break;
                    case 'H': result << std::setw(2) << std::setfill('0') << t.tm_hour; break;
                    case 'M': result << std::setw(2) << std::setfill('0') << t.tm_min; break;
                    case 'S': result << std::setw(2) << std::setfill('0') << t.tm_sec; break;
                    case 'B':
                        {
                            static const char* months[] = {"January","February","March","April","May","June",
                                                           "July","August","September","October","November","December"};
                            result << months[t.tm_mon];
                        }
                        break;
                    case 'b':
                        {
                            static const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                                           "Jul","Aug","Sep","Oct","Nov","Dec"};
                            result << months[t.tm_mon];
                        }
                        break;
                    case 'A':
                        {
                            static const char* days[] = {"Sunday","Monday","Tuesday","Wednesday",
                                                         "Thursday","Friday","Saturday"};
                            result << days[t.tm_wday];
                        }
                        break;
                    case 'a':
                        {
                            static const char* days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
                            result << days[t.tm_wday];
                        }
                        break;
                    case '%': result << '%'; break;
                    default: result << '%' << fmt[i]; break;
                }
            } else {
                result << fmt[i];
            }
        }
        return iris::core::Value(result.str());
    }

    inline iris::core::Value iris_date_time_utc_now() {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        return iris::core::Value(static_cast<double>(ms));
    }
}

#endif //DATE_TIME_LIB_H
