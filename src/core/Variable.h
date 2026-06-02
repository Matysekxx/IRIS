#ifndef VARIABLE_H
#define VARIABLE_H

#include "Value.h"

namespace iris::core {
    struct Variable {
        Value value;
        bool isMutable;
    };
}

#endif //VARIABLE_H
