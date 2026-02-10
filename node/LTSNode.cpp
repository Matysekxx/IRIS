#include "LTSNode.h"

void ProgramNode::execute(RuntimeContext* ctx) {
    for (const auto& stmt : statements) {
        stmt->execute(ctx);
    }
}

void WaitNode::execute(RuntimeContext* ctx) {
    ctx->logger->info("Waiting " + std::to_string(duration) + "ms");
    ctx->driver->sleep(duration);
}

void MouseBlockNode::execute(RuntimeContext* ctx) {
    for (const auto& action : actions) {
        action->execute(ctx);
    }
}

void ClickNode::execute(RuntimeContext* ctx) {
    if (x.has_value() && y.has_value()) {
        ctx->driver->moveMouse(x.value(), y.value());
    }
    
    if (button == Left) {
        ctx->driver->clickMouse(true);
        ctx->logger->info("Click Left");
    } else {
        ctx->driver->clickMouse(false);
        ctx->logger->info("Click Right");
    }
}

void MoveNode::execute(RuntimeContext* ctx) {
    ctx->driver->moveMouse(x, y);
    ctx->logger->info("Move to " + std::to_string(x) + ", " + std::to_string(y));
}

void KeyboardBlockNode::execute(RuntimeContext* ctx) {
    for (const auto& action : actions) {
        action->execute(ctx);
    }
}

void TypeNode::execute(RuntimeContext* ctx) {
    ctx->logger->info("Typing: " + text);
    ctx->driver->typeText(text);
}

void PressNode::execute(RuntimeContext* ctx) {
    ctx->logger->info("Pressing key: " + key);
    ctx->driver->pressKey(key);
}

void HybridClickNode::execute(RuntimeContext* ctx) {
    ClickNode(button).execute(ctx);
}

void HybridPressNode::execute(RuntimeContext* ctx) {
    PressNode(key).execute(ctx);
}