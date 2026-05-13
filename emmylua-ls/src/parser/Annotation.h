#pragma once

#include "parser/Token.h"
#include <string>
#include <vector>
#include <memory>
#include <variant>
#include <optional>

namespace emmy {

// ============================================================
// Type Expression AST
// ============================================================

enum class TypeExprKind {
    Name,         // Simple name: string, number, MyClass
    Nil,
    Boolean,
    Number,
    String,
    Function,     // fun(param: type): returnType
    Table,        // table<key, value> or {[key]: value}
    Array,        // type[]
    Union,        // type1 | type2
    Literal,      // "value", 42, true
    Generic,      // name<T, U>
    Tuple,        // (type1, type2)
    Vararg,       // ...type
};

struct TypeExpr {
    TypeExprKind kind;
    SourceRange range;

    virtual ~TypeExpr() = default;
};

using TypeExprPtr = std::shared_ptr<TypeExpr>;

// Simple named type: string, number, MyClass, module.Class
struct NameTypeExpr : TypeExpr {
    std::string name;

    NameTypeExpr(const std::string& n) : name(n) { kind = TypeExprKind::Name; }
};

// Built-in types
struct NilTypeExpr : TypeExpr {
    NilTypeExpr() { kind = TypeExprKind::Nil; }
};

struct BooleanTypeExpr : TypeExpr {
    BooleanTypeExpr() { kind = TypeExprKind::Boolean; }
};

struct NumberTypeExpr : TypeExpr {
    NumberTypeExpr() { kind = TypeExprKind::Number; }
};

struct StringTypeExpr : TypeExpr {
    StringTypeExpr() { kind = TypeExprKind::String; }
};

// Literal type: "r", 42, true
struct LiteralTypeExpr : TypeExpr {
    std::string value;

    LiteralTypeExpr(const std::string& v) : value(v) { kind = TypeExprKind::Literal; }
};

// Function type: fun(a: number, b: string): boolean
struct FuncTypeExpr : TypeExpr {
    struct Param {
        std::string name;
        TypeExprPtr type;
    };

    std::vector<Param> params;
    std::vector<TypeExprPtr> returnTypes;

    FuncTypeExpr() { kind = TypeExprKind::Function; }
};

// Table type: table<K, V> or {[string]: number}
struct TableTypeExpr : TypeExpr {
    TypeExprPtr keyType;
    TypeExprPtr valueType;

    TableTypeExpr() { kind = TypeExprKind::Table; }
};

// Array type: string[]
struct ArrayTypeExpr : TypeExpr {
    TypeExprPtr elementType;

    ArrayTypeExpr(TypeExprPtr elem) : elementType(std::move(elem)) { kind = TypeExprKind::Array; }
};

// Union type: string | number | nil
struct UnionTypeExpr : TypeExpr {
    std::vector<TypeExprPtr> types;

    UnionTypeExpr() { kind = TypeExprKind::Union; }
};

// Generic type: List<T>, Map<K, V>
struct GenericTypeExpr : TypeExpr {
    std::string baseName;
    std::vector<TypeExprPtr> typeArgs;

    GenericTypeExpr(const std::string& name) : baseName(name) { kind = TypeExprKind::Generic; }
};

// Vararg type: ...string
struct VarargTypeExpr : TypeExpr {
    TypeExprPtr innerType;

    VarargTypeExpr(TypeExprPtr inner) : innerType(std::move(inner)) { kind = TypeExprKind::Vararg; }
};

// ============================================================
// Annotation Tags
// ============================================================

enum class AnnotationTag {
    Class,
    Field,
    Param,
    Return,
    Type,
    Overload,
    Generic,
    Vararg,
    Enum,
    Interface,
    Alias,
    Deprecated,
    Public,
    Protected,
    Private,
    Package,
    See,
    Language,
    Unknown
};

inline AnnotationTag parseAnnotationTag(const std::string& name) {
    if (name == "class")     return AnnotationTag::Class;
    if (name == "field")     return AnnotationTag::Field;
    if (name == "param")     return AnnotationTag::Param;
    if (name == "return")    return AnnotationTag::Return;
    if (name == "type")      return AnnotationTag::Type;
    if (name == "overload")  return AnnotationTag::Overload;
    if (name == "generic")   return AnnotationTag::Generic;
    if (name == "vararg")    return AnnotationTag::Vararg;
    if (name == "enum")      return AnnotationTag::Enum;
    if (name == "interface") return AnnotationTag::Interface;
    if (name == "alias")     return AnnotationTag::Alias;
    if (name == "deprecated")return AnnotationTag::Deprecated;
    if (name == "public")    return AnnotationTag::Public;
    if (name == "protected") return AnnotationTag::Protected;
    if (name == "private")   return AnnotationTag::Private;
    if (name == "package")   return AnnotationTag::Package;
    if (name == "see")       return AnnotationTag::See;
    if (name == "language")  return AnnotationTag::Language;
    return AnnotationTag::Unknown;
}

// ============================================================
// Parsed Annotation Items
// ============================================================

struct AnnotationItem {
    AnnotationTag tag;
    SourcePosition position;
    virtual ~AnnotationItem() = default;
};

using AnnotationItemPtr = std::shared_ptr<AnnotationItem>;

// ---@class Name [: Parent [, Interface1, Interface2]]
struct ClassAnnotation : AnnotationItem {
    std::string name;
    std::vector<std::string> parents;  // Parent class and interfaces
    bool isInterface = false;

    ClassAnnotation() { tag = AnnotationTag::Class; }
};

// ---@field name type
struct FieldAnnotation : AnnotationItem {
    std::string name;
    TypeExprPtr type;
    bool isOptional = false;
    int numericIndex = -1;  // For @field[1] style

    FieldAnnotation() { tag = AnnotationTag::Field; }
};

// ---@param name type [description]
struct ParamAnnotation : AnnotationItem {
    std::string name;
    TypeExprPtr type;
    bool isOptional = false;
    std::string description;

    ParamAnnotation() { tag = AnnotationTag::Param; }
};

// ---@return type [description]
struct ReturnAnnotation : AnnotationItem {
    std::vector<TypeExprPtr> types;
    std::string description;

    ReturnAnnotation() { tag = AnnotationTag::Return; }
};

// ---@type type
struct TypeAnnotation : AnnotationItem {
    TypeExprPtr type;

    TypeAnnotation() { tag = AnnotationTag::Type; }
};

// ---@overload fun(params): return
struct OverloadAnnotation : AnnotationItem {
    TypeExprPtr signature;  // Should be a FuncTypeExpr

    OverloadAnnotation() { tag = AnnotationTag::Overload; }
};

// ---@generic T, U, V
struct GenericAnnotation : AnnotationItem {
    std::vector<std::string> typeParams;

    GenericAnnotation() { tag = AnnotationTag::Generic; }
};

// ---@vararg type
struct VarargAnnotation : AnnotationItem {
    TypeExprPtr type;

    VarargAnnotation() { tag = AnnotationTag::Vararg; }
};

// ---@enum Name [: BaseType]
struct EnumAnnotation : AnnotationItem {
    std::string name;
    std::optional<std::string> baseType;

    EnumAnnotation() { tag = AnnotationTag::Enum; }
};

// ---@interface Name
struct InterfaceAnnotation : AnnotationItem {
    std::string name;

    InterfaceAnnotation() { tag = AnnotationTag::Interface; }
};

// ---@alias Name type
struct AliasAnnotation : AnnotationItem {
    std::string name;
    TypeExprPtr type;

    AliasAnnotation() { tag = AnnotationTag::Alias; }
};

// ---@deprecated
struct DeprecatedAnnotation : AnnotationItem {
    DeprecatedAnnotation() { tag = AnnotationTag::Deprecated; }
};

// ---@see reference
struct SeeAnnotation : AnnotationItem {
    std::string reference;

    SeeAnnotation() { tag = AnnotationTag::See; }
};

// ---@public, ---@protected, ---@private, ---@package
struct AccessAnnotation : AnnotationItem {
    // tag is set to the specific access modifier
};

// ---@language lang
struct LanguageAnnotation : AnnotationItem {
    std::string language;

    LanguageAnnotation() { tag = AnnotationTag::Language; }
};

// ============================================================
// Document Comment Block
// ============================================================

// A complete documentation block parsed from --- comments
struct DocComment {
    std::string description;  // Plain text description (before any @tags)
    std::vector<AnnotationItemPtr> annotations;
    SourcePosition position;
};

}  // namespace emmy
