#ifndef REGEX_LIB_H
#define REGEX_LIB_H

#include "core/Native.h"
#include "core/Value.h"
#include "core/ArrayData.h"
#include <regex>
#include <string>
#include <vector>

namespace iris::std_lib {

    inline iris::core::Value iris_regex_match(iris::core::Value* args, int argCount) {
        if (argCount < 2 || !args[0].isString() || !args[1].isString())
            return iris::core::Value(false);
        try {
            std::regex re(args[0].str());
            return iris::core::Value(std::regex_match(args[1].str(), re));
        } catch (const std::exception&) {
            return iris::core::Value(false);
        }
    }

    inline iris::core::Value iris_regex_find(iris::core::Value* args, int argCount) {
        if (argCount < 2 || !args[0].isString() || !args[1].isString())
            return iris::core::Value(-1);
        try {
            std::regex re(args[0].str());
            std::smatch match;
            std::string input = args[1].str();
            if (std::regex_search(input, match, re)) {
                return iris::core::Value(static_cast<int>(match.position()));
            }
            return iris::core::Value(-1);
        } catch (const std::exception&) {
            return iris::core::Value(-1);
        }
    }

    inline iris::core::Value iris_regex_replace(iris::core::Value* args, int argCount) {
        if (argCount < 3 || !args[0].isString() || !args[1].isString() || !args[2].isString())
            return iris::core::Value("");
        try {
            std::regex re(args[0].str());
            return iris::core::Value(std::regex_replace(args[1].str(), re, args[2].str()));
        } catch (const std::exception&) {
            return iris::core::Value("");
        }
    }

    inline iris::core::Value iris_regex_split(iris::core::Value* args, int argCount) {
        if (argCount < 2 || !args[0].isString() || !args[1].isString())
            return iris::core::Value();
        try {
            std::regex re(args[0].str());
            std::string input = args[1].str();
            std::sregex_token_iterator iter(input.begin(), input.end(), re, -1);
            std::sregex_token_iterator end;
            std::vector<std::string> tokens;
            for (; iter != end; ++iter) {
                tokens.push_back(*iter);
            }

            auto* arr = iris::core::ArrayData::create(tokens.size(), iris::core::ArrayData::VALUE);
            for (size_t i = 0; i < tokens.size(); i++) {
                arr->getValData()[i] = iris::core::Value(tokens[i]);
            }
            return iris::core::Value(arr);
        } catch (const std::exception&) {
            return iris::core::Value();
        }
    }
}

#endif
