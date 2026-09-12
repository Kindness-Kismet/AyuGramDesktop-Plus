/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Api {

enum class SearchFilter {
	NoFilter,
	Pinned,
	Text,
	Photo,
	Video,
	Voice,
	Round,
	File,
	Music,
	Gif,
};

[[nodiscard]] inline bool IsMediaFilter(SearchFilter filter) {
	return (filter == SearchFilter::Photo)
		|| (filter == SearchFilter::Video)
		|| (filter == SearchFilter::Voice)
		|| (filter == SearchFilter::Round)
		|| (filter == SearchFilter::File)
		|| (filter == SearchFilter::Music)
		|| (filter == SearchFilter::Gif);
}

[[nodiscard]] inline bool IsTypeFilter(SearchFilter filter) {
	return (filter == SearchFilter::Text) || IsMediaFilter(filter);
}

} // namespace Api
