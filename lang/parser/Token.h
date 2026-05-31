#ifndef TOKEN_H
#define TOKEN_H

#include <string_view>
#include <string>

namespace iris::parser {
    /**
     * @brief Represents a single token in the source code with its location.
     */
    struct Token {
        std::string_view value;
        int line;
        int column;
        std::string file;

        Token(std::string_view v, int l, int c, std::string f)
            : value(v), line(l), column(c), file(std::move(f)) {}
    };
}

#endif //TOKEN_H
