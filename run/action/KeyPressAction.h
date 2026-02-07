
#ifndef KEYPRESSACTION_H
#define KEYPRESSACTION_H

#include "Action.h"

class KeyPressAction : public Action {
public:
    void run(Instruction instruction) override;
};

#endif //KEYPRESSACTION_H