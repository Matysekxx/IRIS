#ifndef CLICKACTION_H
#define CLICKACTION_H

#include "Action.h"

class ClickAction : public Action {
public:
    void run(Instruction instruction) override;
};

#endif //CLICKACTION_H