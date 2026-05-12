#ifndef NATIVE_REGISTRY_H
#define NATIVE_REGISTRY_H

#include "Native.h"
#include <vector>
#include <unordered_map>
#include <memory>

namespace iris::core {
    /**
     * @brief Singleton registry for native functions and objects.
     */
    class NativeRegistry {
        std::vector<NativeFunction*> functions;
        std::unordered_map<std::string, uint16_t> nameToIndex;

    public:
        static NativeRegistry& getInstance() {
            static NativeRegistry instance;
            return instance;
        }

        uint16_t registerFunction(const std::string& name, NativeFn fn, int arity) {
            uint16_t index = static_cast<uint16_t>(functions.size());
            functions.push_back(new NativeFunction(name, std::move(fn), arity));
            nameToIndex[name] = index;
            return index;
        }

        std::vector<NativeFunction*>& getFunctions() {
            return functions;
        }

        bool hasFunction(const std::string& name) const {
            return nameToIndex.find(name) != nameToIndex.end();
        }

        uint16_t getIndex(const std::string& name) const {
            return nameToIndex.at(name);
        }

        ~NativeRegistry() {
            for (auto f : functions) delete f;
        }
    };
}

#endif //NATIVE_REGISTRY_H
