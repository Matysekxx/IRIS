
#ifndef VARIABLE_H
#define VARIABLE_H
#include "Value.h"

struct Variable {
    Value value;
    bool isMutable;
};

#endif //VARIABLE_H
