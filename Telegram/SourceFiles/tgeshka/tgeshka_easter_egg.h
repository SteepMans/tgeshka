/*
This file is part of TGeshka, an unofficial client
based on Telegram Desktop (GPLv3).

Easter egg (пасхалка) in outgoing messages. The rule and the words live in
tgeshka-easter-egg.txt at the repository root, the same file in all three
TGeshka clients. No Qt here, so the unit test builds with a plain compiler.
*/
#pragma once

#include <string>
#include <vector>

namespace Tgeshka::EasterEgg {

struct Rules {
	std::vector<std::u16string> names;
	std::vector<std::u16string> fillers;
	std::vector<std::u16string> insults;
	std::u16string replacement;
	int maxSuffix = 0;
};

struct TestCase {
	std::u16string input;
	std::u16string output;
};

// Parses the data file (UTF-8). Test cases go to `tests` if it is given.
[[nodiscard]] Rules Parse(
	const std::string &utf8,
	std::vector<TestCase> *tests = nullptr);

// Returns the text with every match replaced, or the same text.
[[nodiscard]] std::u16string Apply(
	const Rules &rules,
	const std::u16string &text);

[[nodiscard]] std::u16string FromUtf8(const std::string &utf8);

} // namespace Tgeshka::EasterEgg
