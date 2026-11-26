/*
This file is part of TGeshka, an unofficial client
based on Telegram Desktop (GPLv3).

Unit test of the easter egg (пасхалка): every case of the [tests] section
in tgeshka-easter-egg.txt. Plain C++, no Qt, see .github/workflows/easter-egg.yml.
*/
#include "tgeshka/tgeshka_easter_egg.h"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace {

std::string ToUtf8(const std::u16string &text) {
	auto result = std::string();
	for (const auto c : text) {
		if (c < 0x80) {
			result.push_back(char(c));
		} else if (c < 0x800) {
			result.push_back(char(0xC0 | (c >> 6)));
			result.push_back(char(0x80 | (c & 0x3F)));
		} else {
			result.push_back(char(0xE0 | (c >> 12)));
			result.push_back(char(0x80 | ((c >> 6) & 0x3F)));
			result.push_back(char(0x80 | (c & 0x3F)));
		}
	}
	return result;
}

} // namespace

int main(int argc, char *argv[]) {
	using namespace Tgeshka::EasterEgg;

	const auto path = (argc > 1) ? argv[1] : "tgeshka-easter-egg.txt";
	auto file = std::ifstream(path, std::ios::binary);
	if (!file) {
		std::printf("FAIL: cannot read %s\n", path);
		return 1;
	}
	auto content = std::stringstream();
	content << file.rdbuf();
	auto tests = std::vector<TestCase>();
	const auto rules = Parse(content.str(), &tests);
	if (rules.names.empty()
		|| rules.insults.empty()
		|| rules.replacement.empty()
		|| tests.empty()) {
		std::printf("FAIL: empty rules or tests\n");
		return 1;
	}
	auto failed = 0;
	for (const auto &test : tests) {
		const auto output = Apply(rules, test.input);
		if (output != test.output) {
			++failed;
			std::printf(
				"FAIL: '%s' -> '%s', expected '%s'\n",
				ToUtf8(test.input).c_str(),
				ToUtf8(output).c_str(),
				ToUtf8(test.output).c_str());
		}
	}
	std::printf(
		"%d of %d cases passed\n",
		int(tests.size()) - failed,
		int(tests.size()));
	return failed ? 1 : 0;
}
