#include "ASTNode.h"

#include <sstream>

void ProgramNode::execute(RuntimeContext *ctx) {
    for (const auto &stmt: statements) {
        stmt->execute(ctx);
    }
}

void WaitNode::execute(RuntimeContext *ctx) {
    ctx->logger->info("Waiting " + std::to_string(duration) + "ms");
    ctx->driver->sleep(duration);
}

void MouseBlockNode::execute(RuntimeContext *ctx) {
    for (const auto &action: actions) {
        action->execute(ctx);
    }
}

void ClickNode::execute(RuntimeContext *ctx) {
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

void MoveNode::execute(RuntimeContext *ctx) {
    ctx->driver->moveMouse(x, y);
    ctx->logger->info("Move to " + std::to_string(x) + ", " + std::to_string(y));
}

void KeyboardBlockNode::execute(RuntimeContext *ctx) {
    for (const auto &action: actions) {
        action->execute(ctx);
    }
}

void TypeNode::execute(RuntimeContext *ctx) {
    ctx->logger->info("Typing: " + text);
    ctx->driver->typeText(text);
}

void PressNode::execute(RuntimeContext *ctx) {
    ctx->logger->info("Pressing key: " + key);
    ctx->driver->pressKey(key);
}

void HybridClickNode::execute(RuntimeContext *ctx) {
    ClickNode(button).execute(ctx);
}

void HybridPressNode::execute(RuntimeContext *ctx) {
    PressNode(key).execute(ctx);
}

Value NumberNode::evaluate(RuntimeContext *ctx) {
    return value;
}

Value VariableNode::evaluate(RuntimeContext *ctx) {
    if (ctx->variables.contains(this->nameOfVariable)) {
        return ctx->variables[this->nameOfVariable];
    }
    throw std::runtime_error("Undefined variable: " + this->nameOfVariable);
}

Value StringNode::evaluate(RuntimeContext *ctx) {
    return value;
}

void VarDeclNode::execute(RuntimeContext *ctx) {
    ctx->variables[this->nameOfVariable] = expression->evaluate(ctx);
}

Value BinaryOperationNode::evaluate(RuntimeContext *ctx) {
    const Value left = this->leftNode->evaluate(ctx);
    const Value right = this->rightNode->evaluate(ctx);

    if (std::holds_alternative<int>(left) && std::holds_alternative<int>(right)) {
        const int leftInt = std::get<int>(left);
        const int rightInt = std::get<int>(right);
        switch (operation) {
            case '+':
                return leftInt + rightInt;
            case '-':
                return leftInt - rightInt;
            case '*':
                return leftInt * rightInt;
            case '/':
                if (rightInt == 0) throw std::runtime_error("Division by zero");
                return leftInt / rightInt;
            default: throw std::runtime_error(std::string("Unknown operator: ") + operation);
        }
    }

    if (operation != '+') {
        throw std::runtime_error("Type mismatch: Cannot perform operation '" + std::string(1, operation) + "' on non-numbers.");
    }
    
    std::string result = std::visit([](auto &&arg1, auto &&arg2) -> std::string {
        auto to_str = []<typename T0>(T0 &&val) -> std::string {
            using T = std::decay_t<T0>;
            if constexpr (std::is_same_v<T, int>) {
                return std::to_string(val);
            } else if constexpr (std::is_same_v<T, std::string>) {
                return val;
            } else {
                return "";
            }
        };

        return to_str(arg1) + to_str(arg2);
    }, left, right);
    return result;
}
