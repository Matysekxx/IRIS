//
// Created by chalo on 07.02.2026.
//

#ifndef KEYPRESSACTION_H
#define KEYPRESSACTION_H

#include "Action.h"

class KeyPressAction : public Action {
public:
    void run(Instruction instruction) override;
};

#endif //KEYPRESSACTION_H