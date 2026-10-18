#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RANGESET_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RANGESET_H_

#include "common/assert.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <vector>

namespace Libs::Graphics {

// Sorts [address, address + size) entries by address and coalesces overlapping or adjacent ones
// in place. Used by the BDA dirty-log fast path to turn unordered dirty/registered ranges into a
// single ordered list that a buffer cursor can walk without a tree search per range.
template <typename Range>
void CoalesceRanges(std::vector<Range>& ranges) {
	std::sort(ranges.begin(), ranges.end(),
	          [](const Range& a, const Range& b) { return a.address < b.address; });
	size_t out = 0;
	for (const auto& range: ranges) {
		if (out != 0) {
			auto&      last      = ranges[out - 1];
			const auto last_end  = last.address + last.size;
			const auto range_end = range.address + range.size;
			if (range.address <= last_end) {
				if (range_end > last_end) {
					last.size = range_end - last.address;
				}
				continue;
			}
		}
		ranges[out++] = range;
	}
	ranges.resize(out);
}

class RangeSet final {
public:
	void Add(uint64_t address, uint64_t size) {
		const auto end = End(address, size);
		auto       it  = m_ranges.lower_bound(address);
		if (it != m_ranges.begin() && std::prev(it)->second >= address) {
			it = std::prev(it);
		}
		uint64_t begin = address;
		uint64_t last  = end;
		while (it != m_ranges.end() && it->first <= last) {
			begin = std::min(begin, it->first);
			last  = std::max(last, it->second);
			it    = m_ranges.erase(it);
		}
		m_ranges.emplace(begin, last);
	}

	void Subtract(uint64_t address, uint64_t size) {
		const auto end = End(address, size);
		auto       it  = m_ranges.lower_bound(address);
		if (it != m_ranges.begin() && std::prev(it)->second > address) {
			it = std::prev(it);
		}
		while (it != m_ranges.end() && it->first < end) {
			const auto begin = it->first;
			const auto last  = it->second;
			it               = m_ranges.erase(it);
			if (begin < address) {
				m_ranges.emplace(begin, address);
			}
			if (last > end) {
				m_ranges.emplace(end, last);
				break;
			}
		}
	}

	void Clear() { m_ranges.clear(); }

	template <typename Func>
	void ForEach(Func&& func) const {
		for (const auto& [begin, end]: m_ranges) {
			func(begin, end);
		}
	}

	[[nodiscard]] bool Intersects(uint64_t address, uint64_t size) const {
		const auto end = End(address, size);
		auto       it  = m_ranges.lower_bound(address);
		if (it != m_ranges.begin() && std::prev(it)->second > address) {
			return true;
		}
		return it != m_ranges.end() && it->first < end;
	}

	[[nodiscard]] bool Contains(uint64_t address, uint64_t size) const {
		const auto end = End(address, size);
		auto       it  = m_ranges.upper_bound(address);
		if (it == m_ranges.begin()) {
			return false;
		}
		--it;
		return it->first <= address && it->second >= end;
	}

	template <typename Func>
	void ForEachInRange(uint64_t address, uint64_t size, Func&& func) const {
		const auto end = End(address, size);
		auto       it  = m_ranges.upper_bound(address);
		if (it != m_ranges.begin()) {
			--it;
		}
		for (; it != m_ranges.end() && it->first < end; ++it) {
			const auto begin = std::max(address, it->first);
			const auto last  = std::min(end, it->second);
			if (begin < last) {
				func(begin, last);
			}
		}
	}

	[[nodiscard]] bool Empty() const { return m_ranges.empty(); }

private:
	static uint64_t End(uint64_t address, uint64_t size) {
		if (size == 0 || size > UINT64_MAX - address) {
			EXIT("invalid range-set address or size\n");
		}
		return address + size;
	}

	std::map<uint64_t, uint64_t> m_ranges;
};

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RANGESET_H_
