// Source/DoTSim/Public/DoTSim/Json.h
// Minimal JSON DOM parser and writer (no exceptions, no RTTI).
// Numbers keep their source text so 64-bit integers round-trip exactly.
#pragma once

#include "DoTSim/Fix64.h"

#include <string>
#include <vector>

namespace dot
{
class DOTSIM_API JsonValue
{
public:
	enum class Type : uint8_t
	{
		Null,
		Bool,
		Number,
		String,
		Array,
		Object
	};

	Type type = Type::Null;
	bool boolValue = false;
	double number = 0.0;
	std::string text; // string value, or the literal text of a number
	std::vector<JsonValue> items; // array items, or object values
	std::vector<std::string> keys; // object keys (parallel to items)

	bool IsNull() const { return type == Type::Null; }
	bool IsObject() const { return type == Type::Object; }
	bool IsArray() const { return type == Type::Array; }
	bool IsNumber() const { return type == Type::Number; }
	bool IsString() const { return type == Type::String; }

	const JsonValue* Find(const char* Key) const;
	bool Has(const char* Key) const { return Find(Key) != nullptr; }
	size_t Size() const { return items.size(); }
	const JsonValue& operator[](size_t Index) const { return items[Index]; }

	double AsDouble(double Default = 0.0) const { return type == Type::Number ? number : Default; }
	int64_t AsInt64(int64_t Default = 0) const;
	int AsInt(int Default = 0) const { return static_cast<int>(AsInt64(Default)); }
	bool AsBool(bool Default = false) const { return type == Type::Bool ? boolValue : Default; }
	const std::string& AsString() const { return text; }
	Fix64 AsFix(Fix64 Default = Fix64()) const { return type == Type::Number ? Fix64::FromDouble(number) : Default; }

	double GetDouble(const char* Key, double Default = 0.0) const;
	int GetInt(const char* Key, int Default = 0) const;
	int64_t GetInt64(const char* Key, int64_t Default = 0) const;
	bool GetBool(const char* Key, bool Default = false) const;
	std::string GetString(const char* Key, const std::string& Default = std::string()) const;
	Fix64 GetFix(const char* Key, Fix64 Default = Fix64()) const;
};

/** Parses UTF-8 JSON text. Returns false and fills Error (with line number) on failure. */
DOTSIM_API bool ParseJson(const std::string& Text, JsonValue& Out, std::string& Error);

/** Small streaming JSON writer producing compact, valid JSON. */
class DOTSIM_API JsonWriter
{
public:
	void BeginObject();
	void EndObject();
	void BeginArray();
	void EndArray();
	void Key(const char* Name);
	void String(const std::string& Value);
	void Int(int64_t Value);
	void UInt(uint64_t Value);
	void Double(double Value);
	void Bool(bool Value);
	void Null();

	const std::string& Result() const { return Out; }

private:
	void Separator();
	void WriteEscaped(const std::string& Value);

	std::string Out;
	std::vector<bool> NeedComma;
	bool bAfterKey = false;
};
} // namespace dot
