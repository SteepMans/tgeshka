/*
This file is part of TGeshka, an unofficial client
based on Telegram Desktop (GPLv3).
*/
#include "tgeshka/tgeshka_easter_egg.h"

#include <algorithm>

namespace Tgeshka::EasterEgg {
namespace {

[[nodiscard]] char16_t Lower(char16_t c) {
	if ((c >= u'A' && c <= u'Z') || (c >= 0x0410 && c <= 0x042F)) {
		return char16_t(c + 32);
	} else if (c == 0x0401 || c == 0x0451) {
		return 0x0435; // ё is е
	}
	return c;
}

[[nodiscard]] bool IsUpper(char16_t c) {
	return (c >= u'A' && c <= u'Z')
		|| (c >= 0x0410 && c <= 0x042F)
		|| (c == 0x0401);
}

[[nodiscard]] bool IsWordChar(char16_t c) {
	return (c >= u'a' && c <= u'z')
		|| (c >= u'A' && c <= u'Z')
		|| (c >= u'0' && c <= u'9')
		|| (c >= 0x0400 && c <= 0x04FF);
}

[[nodiscard]] bool IsSeparator(char16_t c) {
	return (c == u' ')
		|| (c == u'\t')
		|| (c == 0x00A0)
		|| (c == u',')
		|| (c == u'-')
		|| (c == 0x2013)
		|| (c == 0x2014);
}

[[nodiscard]] bool IsSpace(char16_t c) {
	return (c == u' ') || (c == u'\t') || (c == u'\r');
}

[[nodiscard]] std::u16string Normalize(std::u16string word) {
	std::transform(word.begin(), word.end(), word.begin(), Lower);
	return word;
}

[[nodiscard]] std::u16string Trim(const std::u16string &value) {
	auto from = size_t(0);
	auto till = value.size();
	while (from < till && IsSpace(value[from])) {
		++from;
	}
	while (till > from && IsSpace(value[till - 1])) {
		--till;
	}
	return value.substr(from, till - from);
}

[[nodiscard]] bool Contains(
		const std::vector<std::u16string> &list,
		const std::u16string &word) {
	return std::find(list.begin(), list.end(), word) != list.end();
}

[[nodiscard]] bool IsInsult(const Rules &rules, const std::u16string &word) {
	for (const auto &root : rules.insults) {
		if (word.size() >= root.size()
			&& word.size() - root.size() <= size_t(rules.maxSuffix)
			&& word.compare(0, root.size(), root) == 0) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] size_t WordEnd(const std::u16string &text, size_t from) {
	while (from < text.size() && IsWordChar(text[from])) {
		++from;
	}
	return from;
}

[[nodiscard]] size_t SkipSeparators(const std::u16string &text, size_t from) {
	while (from < text.size() && IsSeparator(text[from])) {
		++from;
	}
	return from;
}

[[nodiscard]] std::u16string WordAt(
		const std::u16string &text,
		size_t from,
		size_t till) {
	return Normalize(text.substr(from, till - from));
}

// End of "[filler] insult [insult...]" after a name, or 0 if it is not there.
[[nodiscard]] size_t MatchTail(
		const Rules &rules,
		const std::u16string &text,
		size_t from) {
	auto position = SkipSeparators(text, from);
	auto end = WordEnd(text, position);
	if (end > position && Contains(rules.fillers, WordAt(text, position, end))) {
		position = SkipSeparators(text, end);
		end = WordEnd(text, position);
	}
	auto result = size_t(0);
	while (end > position && IsInsult(rules, WordAt(text, position, end))) {
		result = end;
		position = SkipSeparators(text, end);
		end = WordEnd(text, position);
	}
	return result;
}

} // namespace

std::u16string FromUtf8(const std::string &utf8) {
	auto result = std::u16string();
	const auto byte = [&](size_t index) {
		return char32_t(static_cast<unsigned char>(utf8[index]) & 0x3F);
	};
	for (size_t i = 0; i < utf8.size();) {
		const auto c = char32_t(static_cast<unsigned char>(utf8[i]));
		auto code = c;
		auto length = size_t(1);
		if ((c >> 5) == 0x06 && i + 1 < utf8.size()) {
			code = ((c & 0x1F) << 6) | byte(i + 1);
			length = 2;
		} else if ((c >> 4) == 0x0E && i + 2 < utf8.size()) {
			code = ((c & 0x0F) << 12) | (byte(i + 1) << 6) | byte(i + 2);
			length = 3;
		} else if ((c >> 3) == 0x1E && i + 3 < utf8.size()) {
			code = ((c & 0x07) << 18)
				| (byte(i + 1) << 12)
				| (byte(i + 2) << 6)
				| byte(i + 3);
			length = 4;
		}
		if (code >= 0x10000) {
			code -= 0x10000;
			result.push_back(char16_t(0xD800 + (code >> 10)));
			result.push_back(char16_t(0xDC00 + (code & 0x3FF)));
		} else {
			result.push_back(char16_t(code));
		}
		i += length;
	}
	return result;
}

Rules Parse(const std::string &utf8, std::vector<TestCase> *tests) {
	auto result = Rules();
	const auto text = FromUtf8(utf8);
	auto section = std::u16string();
	auto start = size_t(0);
	while (start < text.size()) {
		auto end = text.find(u'\n', start);
		if (end == std::u16string::npos) {
			end = text.size();
		}
		const auto line = Trim(text.substr(start, end - start));
		start = end + 1;
		if (line.empty() || line[0] == u'#') {
			continue;
		} else if (line.front() == u'[' && line.back() == u']') {
			section = line.substr(1, line.size() - 2);
		} else if (section == u"names") {
			result.names.push_back(Normalize(line));
		} else if (section == u"fillers") {
			result.fillers.push_back(Normalize(line));
		} else if (section == u"insults") {
			result.insults.push_back(Normalize(line));
		} else if (section == u"replacement") {
			result.replacement = line;
		} else if (section == u"max_suffix") {
			result.maxSuffix = 0;
			for (const auto c : line) {
				if (c >= u'0' && c <= u'9') {
					result.maxSuffix = result.maxSuffix * 10 + int(c - u'0');
				}
			}
		} else if (section == u"tests" && tests) {
			const auto arrow = line.find(u" => ");
			if (arrow != std::u16string::npos) {
				tests->push_back({ line.substr(0, arrow), line.substr(arrow + 4) });
			}
		}
	}
	return result;
}

std::u16string Apply(const Rules &rules, const std::u16string &text) {
	auto result = std::u16string();
	auto copied = size_t(0);
	auto replaced = false;
	auto position = size_t(0);
	while (position < text.size()) {
		if (!IsWordChar(text[position])) {
			++position;
			continue;
		}
		const auto end = WordEnd(text, position);
		const auto tail = Contains(rules.names, WordAt(text, position, end))
			? MatchTail(rules, text, end)
			: size_t(0);
		if (!tail) {
			position = end;
			continue;
		}
		result += text.substr(copied, position - copied);
		result += IsUpper(text[position]) ? u"Костя " : u"костя ";
		result += rules.replacement;
		copied = position = tail;
		replaced = true;
	}
	if (!replaced) {
		return text;
	}
	result += text.substr(copied);
	return result;
}

} // namespace Tgeshka::EasterEgg
