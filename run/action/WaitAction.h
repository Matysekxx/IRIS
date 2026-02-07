#ifndef WAITACTION_H
#define WAITACTION_H

#include "Action.h"

class WaitAction : public Action {
public:
    void run(Instruction instruction) override;
};

#endif //WAITACTION_H