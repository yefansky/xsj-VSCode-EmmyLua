#pragma once

#include "parser/Annotation.h"
#include "parser/Lexer.h"
#include <string>
#include <vector>

namespace emmy {

// Parses EmmyLua annotation text (from --- comments) into structured data
class AnnotationParser {
public:
    // Parse annotation comments collected by the Lexer
    static std::vector<DocComment> parse(const std::vector<Lexer::Comment>& comments);

    // Parse a single annotation block text
    static DocComment parseBlock(const std::string& text, const SourcePosition& position);

private:
    // Internal parsing state
    struct ParseContext {
        std::string text;
        int pos = 0;

        char current() const;
        char peek(int offset = 1) const;
        void advance();
        void skipWhitespace();
        bool atEnd() const;

        // Read a word (identifier)
        std::string readWord();
        // Read until end of line
        std::string readLine();
        // Read a type expression string (handles < >, |, etc.)
        std::string readTypeExpr();
        // Parse a type expression from a string
        static TypeExprPtr parseTypeExpr(const std::string& text);
    };

    static AnnotationItemPtr parseAnnotation(ParseContext& ctx);
    static std::shared_ptr<ClassAnnotation> parseClass(ParseContext& ctx, bool isInterface = false);
    static std::shared_ptr<FieldAnnotation> parseField(ParseContext& ctx);
    static std::shared_ptr<ParamAnnotation> parseParam(ParseContext& ctx);
    static std::shared_ptr<ReturnAnnotation> parseReturn(ParseContext& ctx);
    static std::shared_ptr<TypeAnnotation> parseType(ParseContext& ctx);
    static std::shared_ptr<OverloadAnnotation> parseOverload(ParseContext& ctx);
    static std::shared_ptr<GenericAnnotation> parseGeneric(ParseContext& ctx);
    static std::shared_ptr<VarargAnnotation> parseVararg(ParseContext& ctx);
    static std::shared_ptr<EnumAnnotation> parseEnum(ParseContext& ctx);
    static std::shared_ptr<AliasAnnotation> parseAlias(ParseContext& ctx);
    static std::shared_ptr<SeeAnnotation> parseSee(ParseContext& ctx);
    static std::shared_ptr<LanguageAnnotation> parseLanguage(ParseContext& ctx);
};

}  // namespace emmy
