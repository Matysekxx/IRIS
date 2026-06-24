#ifndef PARSER_H
#define PARSER_H
#include <fstream>
#include <iosfwd>
#include <vector>
#include <memory>
#include <unordered_set>
#include <string>
#include "frontend/ASTNode.h"
#include "NodeFactory.h"
#include "log/Logger.h"

#include "Token.h"

namespace iris::parser {
    /**
     * @brief Recursive Descent Parser for the IRIS language.
     * 
     * The Parser takes source code (either from a file or string), tokenizes it,
     * and constructs an Abstract Syntax Tree (AST). It handles:
     * - Tokenization (Lexing)
     * - Syntax analysis
     * - Import management and circular dependency prevention
     * - Error reporting via Logger
     */
    class Parser {
    private:
        std::ifstream file; ///< Input file stream
        iris::log::Logger *logger; ///< Logger for error reporting
        std::string filePath; ///< Path to the current source file
        std::string sourceCode; ///< Raw source code content
        std::unordered_set<std::string> *sharedImports; ///< Set of already imported files (across parsers)
        std::unique_ptr<std::unordered_set<std::string> > rootImports; ///< Set of imports if this is the root parser

        std::vector<Token> tokens; ///< List of tokens generated from source
        size_t currentToken = 0; ///< Current token index during parsing
        std::unordered_map<int, std::string> docComments; ///< line → doc comment text from /// or /** */

        NodeFactory factory; ///< Factory for creating AST nodes

        /** @brief Converts source code into a stream of tokens. */
        void tokenize(std::string_view source);

        /** @brief Parses a full program (list of statements). */
        std::unique_ptr<iris::node::ProgramNode> parseProgram();

        /** @brief Parses a single statement. */
        std::unique_ptr<iris::node::ASTNode> parseStatement();

        std::unique_ptr<iris::node::ProgramNode> program; ///< The resulting AST root

    public:
        /**
         * @brief Constructs a parser for the specified file.
         *
         * @param filePath Path to the .iris file.
         * @param logger Logger instance for errors.
         * @param sharedImports Optional set of shared imports to prevent double-loading.
         */
        Parser(const std::string &filePath, iris::log::Logger *logger,
               std::unordered_set<std::string> *sharedImports = nullptr);

        /** @brief Starts the parsing process. Results can be retrieved via getProgram(). */
        void parse();

        /** @brief Returns the root of the constructed AST. */
        [[nodiscard]] iris::node::ProgramNode *getProgram() const { return program.get(); }

        ~Parser();
    };
}

#endif //PARSER_H
