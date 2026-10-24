// SimTools/ToolCommon.h
// Helpers shared by the command-line tools: stdio content source and scenario loading.
#pragma once

#include "DoTSim/Content.h"
#include "DoTSim/Replay.h"

#include <cstdio>
#include <string>
#include <vector>

namespace dottools
{
inline bool ReadFileText(const std::string& Path, std::string& Out)
{
	std::FILE* F = std::fopen(Path.c_str(), "rb");
	if (!F)
	{
		return false;
	}
	Out.clear();
	// Heap buffer: small-stack targets (WebAssembly defaults to 64 KB) must not hold it on the stack.
	std::vector<char> Buffer(65536);
	size_t N = 0;
	while ((N = std::fread(Buffer.data(), 1, Buffer.size(), F)) > 0)
	{
		Out.append(Buffer.data(), N);
	}
	std::fclose(F);
	return true;
}

inline bool WriteFileText(const std::string& Path, const std::string& Text)
{
	std::FILE* F = std::fopen(Path.c_str(), "wb");
	if (!F)
	{
		return false;
	}
	const size_t Written = std::fwrite(Text.data(), 1, Text.size(), F);
	std::fclose(F);
	return Written == Text.size();
}

class FileContentSource : public dot::IContentSource
{
public:
	explicit FileContentSource(std::string InRoot) : Root(std::move(InRoot)) {}
	bool ReadText(const std::string& RelativePath, std::string& OutText) override { return ReadFileText(Root + "/" + RelativePath, OutText); }

private:
	std::string Root;
};

inline bool LoadContentDir(const std::string& Dir, dot::SimContent& Out, std::string& Error)
{
	FileContentSource Source(Dir);
	return dot::LoadContent(Source, Out, Error);
}

inline bool LoadScenario(const std::string& Dir, const std::string& RelPath, dot::BattleSetup& Out, std::string& Error)
{
	std::string Text;
	if (!ReadFileText(Dir + "/" + RelPath, Text))
	{
		Error = "cannot read scenario " + RelPath;
		return false;
	}
	dot::JsonValue Json;
	if (!dot::ParseJson(Text, Json, Error))
	{
		return false;
	}
	return dot::ParseBattleSetup(Json, Out, Error);
}

inline const char* DefaultContentDir()
{
#ifdef DOT_DEFAULT_CONTENT_DIR
	return DOT_DEFAULT_CONTENT_DIR;
#else
	return "Content/Data";
#endif
}
} // namespace dottools
