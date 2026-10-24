// Source/DoTSim/Private/Json.cpp
#include "DoTSim/Json.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace dot
{
const JsonValue* JsonValue::Find(const char* Key) const
{
	if (type != Type::Object)
	{
		return nullptr;
	}
	for (size_t I = 0; I < keys.size(); ++I)
	{
		if (keys[I] == Key)
		{
			return &items[I];
		}
	}
	return nullptr;
}

int64_t JsonValue::AsInt64(int64_t Default) const
{
	if (type != Type::Number)
	{
		return Default;
	}
	// Integers are parsed from text to keep full 64-bit precision.
	if (text.find_first_of(".eE") == std::string::npos)
	{
		return static_cast<int64_t>(std::strtoll(text.c_str(), nullptr, 10));
	}
	return static_cast<int64_t>(number);
}

double JsonValue::GetDouble(const char* Key, double Default) const
{
	const JsonValue* V = Find(Key);
	return V ? V->AsDouble(Default) : Default;
}

int JsonValue::GetInt(const char* Key, int Default) const
{
	const JsonValue* V = Find(Key);
	return V ? V->AsInt(Default) : Default;
}

int64_t JsonValue::GetInt64(const char* Key, int64_t Default) const
{
	const JsonValue* V = Find(Key);
	return V ? V->AsInt64(Default) : Default;
}

bool JsonValue::GetBool(const char* Key, bool Default) const
{
	const JsonValue* V = Find(Key);
	return V ? V->AsBool(Default) : Default;
}

std::string JsonValue::GetString(const char* Key, const std::string& Default) const
{
	const JsonValue* V = Find(Key);
	return (V && V->type == Type::String) ? V->text : Default;
}

Fix64 JsonValue::GetFix(const char* Key, Fix64 Default) const
{
	const JsonValue* V = Find(Key);
	return V ? V->AsFix(Default) : Default;
}

namespace
{
class JsonParser
{
public:
	explicit JsonParser(const std::string& InText) : Src(InText) {}

	bool Parse(JsonValue& Out, std::string& Error)
	{
		SkipWhitespace();
		if (!ParseValue(Out, 0))
		{
			Error = Message;
			return false;
		}
		SkipWhitespace();
		if (Pos != Src.size())
		{
			Fail("unexpected trailing characters");
			Error = Message;
			return false;
		}
		return true;
	}

private:
	bool Fail(const char* What)
	{
		if (Message.empty())
		{
			int Line = 1;
			for (size_t I = 0; I < Pos && I < Src.size(); ++I)
			{
				if (Src[I] == '\n')
				{
					++Line;
				}
			}
			Message = std::string("JSON error at line ") + std::to_string(Line) + ": " + What;
		}
		return false;
	}

	void SkipWhitespace()
	{
		while (Pos < Src.size())
		{
			const char C = Src[Pos];
			if (C == ' ' || C == '\t' || C == '\n' || C == '\r')
			{
				++Pos;
			}
			else if (C == '/' && Pos + 1 < Src.size() && Src[Pos + 1] == '/')
			{
				// Allow // comments in content files.
				while (Pos < Src.size() && Src[Pos] != '\n')
				{
					++Pos;
				}
			}
			else
			{
				break;
			}
		}
	}

	bool ParseValue(JsonValue& Out, int Depth)
	{
		if (Depth > 64)
		{
			return Fail("nesting too deep");
		}
		if (Pos >= Src.size())
		{
			return Fail("unexpected end of input");
		}
		const char C = Src[Pos];
		if (C == '{')
		{
			return ParseObject(Out, Depth);
		}
		if (C == '[')
		{
			return ParseArray(Out, Depth);
		}
		if (C == '"')
		{
			Out.type = JsonValue::Type::String;
			return ParseString(Out.text);
		}
		if (C == 't' || C == 'f' || C == 'n')
		{
			return ParseLiteral(Out);
		}
		if (C == '-' || (C >= '0' && C <= '9'))
		{
			return ParseNumber(Out);
		}
		return Fail("unexpected character");
	}

	bool ParseLiteral(JsonValue& Out)
	{
		if (Src.compare(Pos, 4, "true") == 0)
		{
			Out.type = JsonValue::Type::Bool;
			Out.boolValue = true;
			Pos += 4;
			return true;
		}
		if (Src.compare(Pos, 5, "false") == 0)
		{
			Out.type = JsonValue::Type::Bool;
			Out.boolValue = false;
			Pos += 5;
			return true;
		}
		if (Src.compare(Pos, 4, "null") == 0)
		{
			Out.type = JsonValue::Type::Null;
			Pos += 4;
			return true;
		}
		return Fail("invalid literal");
	}

	bool ParseNumber(JsonValue& Out)
	{
		const size_t Start = Pos;
		if (Src[Pos] == '-')
		{
			++Pos;
		}
		bool bDigits = false;
		while (Pos < Src.size())
		{
			const char C = Src[Pos];
			if ((C >= '0' && C <= '9') || C == '.' || C == 'e' || C == 'E' || C == '+' || C == '-')
			{
				bDigits = bDigits || (C >= '0' && C <= '9');
				++Pos;
			}
			else
			{
				break;
			}
		}
		if (!bDigits)
		{
			return Fail("invalid number");
		}
		Out.type = JsonValue::Type::Number;
		Out.text = Src.substr(Start, Pos - Start);
		Out.number = std::strtod(Out.text.c_str(), nullptr);
		return true;
	}

	static void AppendUtf8(std::string& S, uint32_t Code)
	{
		if (Code < 0x80)
		{
			S.push_back(static_cast<char>(Code));
		}
		else if (Code < 0x800)
		{
			S.push_back(static_cast<char>(0xC0 | (Code >> 6)));
			S.push_back(static_cast<char>(0x80 | (Code & 0x3F)));
		}
		else
		{
			S.push_back(static_cast<char>(0xE0 | (Code >> 12)));
			S.push_back(static_cast<char>(0x80 | ((Code >> 6) & 0x3F)));
			S.push_back(static_cast<char>(0x80 | (Code & 0x3F)));
		}
	}

	bool ParseString(std::string& Out)
	{
		++Pos; // opening quote
		Out.clear();
		while (Pos < Src.size())
		{
			const char C = Src[Pos++];
			if (C == '"')
			{
				return true;
			}
			if (C != '\\')
			{
				Out.push_back(C);
				continue;
			}
			if (Pos >= Src.size())
			{
				break;
			}
			const char E = Src[Pos++];
			switch (E)
			{
			case '"': Out.push_back('"'); break;
			case '\\': Out.push_back('\\'); break;
			case '/': Out.push_back('/'); break;
			case 'b': Out.push_back('\b'); break;
			case 'f': Out.push_back('\f'); break;
			case 'n': Out.push_back('\n'); break;
			case 'r': Out.push_back('\r'); break;
			case 't': Out.push_back('\t'); break;
			case 'u':
			{
				if (Pos + 4 > Src.size())
				{
					return Fail("bad unicode escape");
				}
				const std::string Hex = Src.substr(Pos, 4);
				Pos += 4;
				AppendUtf8(Out, static_cast<uint32_t>(std::strtoul(Hex.c_str(), nullptr, 16)));
				break;
			}
			default: return Fail("bad escape");
			}
		}
		return Fail("unterminated string");
	}

	bool ParseArray(JsonValue& Out, int Depth)
	{
		Out.type = JsonValue::Type::Array;
		++Pos;
		SkipWhitespace();
		if (Pos < Src.size() && Src[Pos] == ']')
		{
			++Pos;
			return true;
		}
		while (true)
		{
			Out.items.emplace_back();
			SkipWhitespace();
			if (!ParseValue(Out.items.back(), Depth + 1))
			{
				return false;
			}
			SkipWhitespace();
			if (Pos >= Src.size())
			{
				return Fail("unterminated array");
			}
			if (Src[Pos] == ',')
			{
				++Pos;
				continue;
			}
			if (Src[Pos] == ']')
			{
				++Pos;
				return true;
			}
			return Fail("expected , or ]");
		}
	}

	bool ParseObject(JsonValue& Out, int Depth)
	{
		Out.type = JsonValue::Type::Object;
		++Pos;
		SkipWhitespace();
		if (Pos < Src.size() && Src[Pos] == '}')
		{
			++Pos;
			return true;
		}
		while (true)
		{
			SkipWhitespace();
			if (Pos >= Src.size() || Src[Pos] != '"')
			{
				return Fail("expected object key");
			}
			std::string Key;
			if (!ParseString(Key))
			{
				return false;
			}
			SkipWhitespace();
			if (Pos >= Src.size() || Src[Pos] != ':')
			{
				return Fail("expected :");
			}
			++Pos;
			SkipWhitespace();
			Out.keys.push_back(Key);
			Out.items.emplace_back();
			if (!ParseValue(Out.items.back(), Depth + 1))
			{
				return false;
			}
			SkipWhitespace();
			if (Pos >= Src.size())
			{
				return Fail("unterminated object");
			}
			if (Src[Pos] == ',')
			{
				++Pos;
				continue;
			}
			if (Src[Pos] == '}')
			{
				++Pos;
				return true;
			}
			return Fail("expected , or }");
		}
	}

	const std::string& Src;
	size_t Pos = 0;
	std::string Message;
};
} // namespace

bool ParseJson(const std::string& Text, JsonValue& Out, std::string& Error)
{
	Out = JsonValue();
	JsonParser Parser(Text);
	return Parser.Parse(Out, Error);
}

void JsonWriter::Separator()
{
	if (bAfterKey)
	{
		bAfterKey = false;
		return;
	}
	if (!NeedComma.empty())
	{
		if (NeedComma.back())
		{
			Out.push_back(',');
		}
		NeedComma.back() = true;
	}
}

void JsonWriter::BeginObject()
{
	Separator();
	Out.push_back('{');
	NeedComma.push_back(false);
}

void JsonWriter::EndObject()
{
	Out.push_back('}');
	NeedComma.pop_back();
}

void JsonWriter::BeginArray()
{
	Separator();
	Out.push_back('[');
	NeedComma.push_back(false);
}

void JsonWriter::EndArray()
{
	Out.push_back(']');
	NeedComma.pop_back();
}

void JsonWriter::Key(const char* Name)
{
	Separator();
	WriteEscaped(Name);
	Out.push_back(':');
	bAfterKey = true;
}

void JsonWriter::String(const std::string& Value)
{
	Separator();
	WriteEscaped(Value);
}

void JsonWriter::WriteEscaped(const std::string& Value)
{
	Out.push_back('"');
	for (const char C : Value)
	{
		switch (C)
		{
		case '"': Out += "\\\""; break;
		case '\\': Out += "\\\\"; break;
		case '\n': Out += "\\n"; break;
		case '\r': Out += "\\r"; break;
		case '\t': Out += "\\t"; break;
		default: Out.push_back(C); break;
		}
	}
	Out.push_back('"');
}

void JsonWriter::Int(int64_t Value)
{
	Separator();
	Out += std::to_string(Value);
}

void JsonWriter::UInt(uint64_t Value)
{
	Separator();
	Out += std::to_string(Value);
}

void JsonWriter::Double(double Value)
{
	Separator();
	char Buffer[64];
	std::snprintf(Buffer, sizeof(Buffer), "%.17g", Value);
	Out += Buffer;
}

void JsonWriter::Bool(bool Value)
{
	Separator();
	Out += Value ? "true" : "false";
}

void JsonWriter::Null()
{
	Separator();
	Out += "null";
}
} // namespace dot
