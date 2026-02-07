//
// Created by chalo on 07.02.2026.
//

#ifndef MOVEMOUSEACTION_H
#define MOVEMOUSEACTION_H
#include "Action.h"


class MoveMouseAction : public Action {
public:
    void run(Instruction instruction) override;
};



#endif //MOVEMOUSEACTION_H
