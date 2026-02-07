

#ifndef ACTION_H
#define ACTION_H

#include "../../parser/Instruction.h"


class Action {
public:
    virtual ~Action() = default;

    virtual void run(Instruction instruction) = 0;
};

#endif //ACTION_H
