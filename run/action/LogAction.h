#ifndef LOGACTION_H
#define LOGACTION_H

#include "Action.h"
#include "../../log/Logger.h"

class LogAction : public Action {
    Logger* logger;
public:
    explicit LogAction(Logger* logger);
    void run(Instruction instruction) override;
};

#endif //LOGACTION_H