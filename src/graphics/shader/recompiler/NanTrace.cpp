#include "graphics/shader/recompiler/NanTrace.h"

#include "common/logging/log.h"

#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <mutex>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Libs::Graphics::ShaderRecompiler::NanTrace {

namespace {

struct SlotInfo {
	uint64_t    shader_hash = 0;
	const char* stage       = "";
	Site        site        = Site::ColorExport;
	uint32_t    index       = 0;
	bool        infinity    = false;
};

struct SlotKey {
	uint64_t  shader_hash = 0;
	uintptr_t stage       = 0; // stage names are string literals
	uint64_t  packed      = 0;

	bool operator==(const SlotKey& other) const {
		return shader_hash == other.shader_hash && stage == other.stage && packed == other.packed;
	}
};

struct SlotKeyHash {
	size_t operator()(const SlotKey& key) const {
		return static_cast<size_t>(key.shader_hash ^ (key.packed * 0x9e3779b97f4a7c15ull) ^
		                           (static_cast<uint64_t>(key.stage) << 7u));
	}
};

constexpr uint32_t MaxLoggedHits = 400;

std::atomic<uint64_t>                             g_device_address {0};
std::mutex                                        g_mutex;
std::vector<SlotInfo>                             g_slots;
std::unordered_map<SlotKey, uint32_t, SlotKeyHash> g_slot_index;
std::vector<uint8_t>                              g_reported;
uint32_t                                          g_logged_hits   = 0;
bool                                              g_overflow_logged = false;
struct EventRecord {
	uint32_t token = 0;
	EventInfo info;
	uint32_t input_slot = UINT32_MAX;
};
std::vector<EventRecord> g_events;
uint32_t g_last_event = 0;
struct InputRecord {
	uint32_t token = 0;
	std::vector<StageInputs> stages;
};
std::vector<InputRecord> g_inputs;
uint32_t g_next_input = 0;

const char* SiteName(Site site) {
	switch (site) {
		case Site::ColorExport: return "colour export MRT";
		case Site::PositionExport: return "position export";
		case Site::ImageStore: return "image store binding";
	}
	return "site";
}

} // namespace

bool Requested() {
	static const bool requested = [] {
		const char* value = std::getenv("KYTY_UFC_NAN_TRACE");
		return value != nullptr && std::strcmp(value, "1") == 0;
	}();
	return requested;
}

void Activate(uint64_t device_address) {
	{
		std::lock_guard lock(g_mutex);
		std::fill(g_reported.begin(), g_reported.end(), 0);
		g_logged_hits = 0;
		g_events.assign(MaxEvents, {});
		g_last_event = 0;
		g_inputs.clear();
		g_inputs.resize(MaxInputEvents);
		g_next_input = 0;
	}
	g_device_address.store(device_address, std::memory_order_release);
	Log::WriteToConsoleAndLog(
	    "NaN trace: ON (KYTY_UFC_NAN_TRACE=1). Shaders report the first NaN/Inf each colour export, "
	    "position export and image store writes; 'NaN trace hit' uses completed snapshots. "
	    "Stamps are atomic counter order, not causal GPU event order; frame/tick are snapshot boundaries. "
	    "Event tokens identify recorded work; raw output words are from the winning invocation. "
	    "Output is not changed.\n");
}

void Deactivate() {
	g_device_address.store(0, std::memory_order_release);
}

bool Active() {
	return g_device_address.load(std::memory_order_acquire) != 0;
}

uint64_t DeviceAddress() {
	return g_device_address.load(std::memory_order_acquire);
}

bool SelectInputShader(uint64_t shader_hash) {
	return shader_hash == 0x2c4e5b0c45d615ebull || shader_hash == 0xc44c878e59f9e5ffull ||
	       shader_hash == 0x343814752fbf40a9ull || shader_hash == 0x7863cceac4a3fa7eull;
}

uint64_t InputBytes(std::span<const StageInputs> inputs) {
	uint64_t bytes = inputs.size_bytes();
	for (const auto& stage: inputs) {
		const auto& r = stage.resources;
		bytes += (r.buffers.size() + r.images.size() + r.samplers.size()) * sizeof(IR::DescriptorValue);
		bytes += (r.flattened_srt.size() + r.user_data.size() + stage.shader_data.size() +
		          stage.user_data_registers.size()) * sizeof(uint32_t);
		bytes += r.specialization_reads.size() * sizeof(r.specialization_reads.front());
		bytes += stage.buffers.size() * sizeof(StageInputs::BufferBinding);
		bytes += stage.images.size() * sizeof(StageInputs::ImageBinding);
	}
	return bytes;
}

uint32_t RecordEvent(const EventInfo& info, std::vector<StageInputs> inputs) {
	if (!Active()) return 0;
	std::lock_guard lock(g_mutex);
	if (g_last_event == UINT32_MAX || g_events.empty()) return 0;
	const auto token = ++g_last_event;
	g_events[token % MaxEvents] = {token, info};
	if (!inputs.empty() && InputBytes(inputs) <= MaxInputBytes) {
		const auto slot = g_next_input++ % MaxInputEvents;
		for (auto& stage: inputs) stage.push_words[EventPushDword] = token;
		g_inputs[slot] = {token, std::move(inputs)};
		g_events[token % MaxEvents].input_slot = slot;
	}
	return token;
}

bool LookupEvent(uint32_t token, EventInfo& info) {
	std::lock_guard lock(g_mutex);
	if (token == 0 || g_events.empty()) return false;
	const auto& record = g_events[token % MaxEvents];
	if (record.token != token) return false;
	info = record.info;
	return true;
}

bool LookupInputs(uint32_t token, std::vector<StageInputs>& inputs) {
	std::lock_guard lock(g_mutex);
	inputs.clear();
	if (token == 0 || g_events.empty()) return false;
	const auto& event = g_events[token % MaxEvents];
	if (event.token != token || event.input_slot == UINT32_MAX) return false;
	const auto& record = g_inputs[event.input_slot];
	if (record.token != token) return false;
	inputs = record.stages; // Deep owned copy survives subsequent ring reuse.
	return true;
}

uint32_t RegisterSlot(uint64_t shader_hash, const char* stage, Site site, uint32_t index,
                      bool infinity) {
	const SlotKey key {
	    .shader_hash = shader_hash,
	    .stage       = reinterpret_cast<uintptr_t>(stage),
	    .packed = (static_cast<uint64_t>(site) << 33u) | (static_cast<uint64_t>(index) << 1u) |
	              (infinity ? 1u : 0u),
	};
	std::lock_guard lock(g_mutex);
	if (const auto it = g_slot_index.find(key); it != g_slot_index.end()) {
		return it->second;
	}
	if (g_slots.size() >= MaxSlots) {
		if (!g_overflow_logged) {
			g_overflow_logged = true;
			Log::WriteToConsoleAndLog("NaN trace: all slots in use; later shaders are not traced\n");
		}
		return NoSlot;
	}
	const auto slot = static_cast<uint32_t>(g_slots.size());
	g_slots.push_back({.shader_hash = shader_hash,
	                   .stage       = stage != nullptr ? stage : "",
	                   .site        = site,
	                   .index       = index,
	                   .infinity    = infinity});
	g_reported.push_back(0);
	g_slot_index.emplace(key, slot);
	return slot;
}

void Poll(uint64_t frame, uint64_t tick, std::span<const uint32_t> words) {
	if (!Active() || words.size() != HeaderWords + MaxSlots * SlotWords) {
		return;
	}

	std::vector<std::pair<uint32_t, uint32_t>> hits; // (order, slot)
	std::vector<SlotInfo>                       infos;
	{
		std::lock_guard lock(g_mutex);
		const auto      count = static_cast<uint32_t>(g_slots.size());
		for (uint32_t slot = 0; slot < count; slot++) {
			if (g_reported[slot] != 0) {
				continue;
			}
			const uint32_t order = words[HeaderWords + slot * SlotWords];
			if (order != 0) {
				g_reported[slot] = 1;
				hits.emplace_back(order, slot);
			}
		}
		std::sort(hits.begin(), hits.end());
		infos.reserve(hits.size());
		for (const auto& hit: hits) {
			infos.push_back(g_slots[hit.second]);
		}
	}
	for (size_t i = 0; i < hits.size(); i++) {
		if (g_logged_hits >= MaxLoggedHits) {
			if (g_logged_hits == MaxLoggedHits) {
				g_logged_hits++;
				Log::WriteToConsoleAndLog("NaN trace: more hits not logged\n");
			}
			return;
		}
		g_logged_hits++;
		const auto& info = infos[i];
		const auto* record = words.data() + HeaderWords + hits[i].second * SlotWords;
		char        line[512];
		std::snprintf(line, sizeof(line),
		              "NaN trace hit #%u snapshot frame %" PRIu64 " tick %" PRIu64 ": %s 0x%016" PRIx64
		              " %s%u wrote %s event=%u raw=%08x/%08x/%08x/%08x\n",
		              hits[i].first, frame, tick, info.stage, info.shader_hash, SiteName(info.site),
		              info.index, info.infinity ? "Inf" : "NaN", record[1], record[2], record[3], record[4], record[5]);
		Log::WriteToConsoleAndLog(line);
		EventInfo event;
		if (LookupEvent(record[1], event)) {
			std::snprintf(line, sizeof(line),
			              "NaN trace event %u: recorded frame %" PRIu64 " tick %" PRIu64
			              " submit %" PRIu64 " op=%u args=%08x/%08x/%08x/%08x address=0x%016" PRIx64 "\n",
			              record[1], event.frame, event.tick, event.submit, event.op,
			              event.args[0], event.args[1], event.args[2], event.args[3], event.address);
		} else {
			std::snprintf(line, sizeof(line), "NaN trace event %u: history unavailable (zero/expired); no attribution\n", record[1]);
		}
		Log::WriteToConsoleAndLog(line);
		if (SelectInputShader(info.shader_hash)) {
			std::vector<StageInputs> inputs;
			if (!LookupInputs(record[1], inputs)) {
				Log::WriteToConsoleAndLog("NaN trace inputs: unavailable (expired/oversized/not selected); no live-memory fallback\n");
				continue;
			}
			for (const auto& stage: inputs) {
				if (stage.shader_hash != info.shader_hash) continue;
				std::snprintf(line, sizeof(line),
				              "NaN trace inputs event=%u shader=0x%016" PRIx64
				              " stage=%u wave=%u sgpr_base=%u bda=%u address_writes=%u data_push=%u data_start=%u bytes=%" PRIu64
				              " (CPU snapshot only; GPU contents/vertex identity not captured)\n",
				              record[1], stage.shader_hash, stage.stage, stage.wave_size, stage.user_data_base,
				              stage.uses_bda, stage.address_writes, stage.shader_data_in_push,
				              stage.shader_data_start, InputBytes(std::span(&stage, 1)));
				Log::WriteToConsoleAndLog(line);
				auto log_words = [&](const char* kind, uint32_t index, std::span<const uint32_t> data) {
					for (size_t at = 0; at < data.size(); at += 8) {
						std::string text = "NaN trace input event=" + std::to_string(record[1]) + " " + kind +
						                   "[" + std::to_string(index) + "] word=" + std::to_string(at) + ":";
						for (size_t j = at; j < std::min(at + 8, data.size()); ++j) {
							char word[16];
							std::snprintf(word, sizeof(word), " %08x", data[j]);
							text += word;
						}
						Log::WriteToConsoleAndLog((text + "\n").c_str());
					}
				};
				log_words("sgpr", stage.user_data_base, stage.resources.user_data);
				log_words("flat_srt", 0, stage.resources.flattened_srt);
				log_words("shader_data", 0, stage.shader_data);
				log_words("shader_data_sgpr_map", 0, stage.user_data_registers);
				log_words("push", 0, stage.push_words);
				auto log_descriptors = [&](const char* kind, const auto& descriptors) {
					for (uint32_t j = 0; j < descriptors.size(); ++j) {
						log_words(kind, j, std::span(descriptors[j].dwords).first(descriptors[j].dword_count));
					}
				};
				log_descriptors("buffer_desc", stage.resources.buffers);
				log_descriptors("image_desc", stage.resources.images);
				log_descriptors("sampler_desc", stage.resources.samplers);
				for (const auto& binding: stage.buffers) {
					std::snprintf(line, sizeof(line), "NaN trace buffer event=%u resource=%u guest=0x%016" PRIx64
					              " size=%" PRIu64 " native_offset=%" PRIu64 " native_range=%" PRIu64 "\n",
					              record[1], binding.resource, binding.address, binding.size,
					              binding.native_offset, binding.native_range);
					Log::WriteToConsoleAndLog(line);
				}
				for (uint32_t j = 0; j < stage.images.size(); ++j) {
					const auto& binding = stage.images[j];
					std::snprintf(line, sizeof(line), "NaN trace image event=%u resource=%u guest=0x%016" PRIx64
					              " size=%" PRIu64 " guest_format=%u native_format=%u mip=%u+%u layer=%u+%u\n",
					              record[1], j, binding.address, binding.size, binding.guest_format,
					              binding.native_format, binding.base_level, binding.level_count,
					              binding.base_layer, binding.layer_count);
					Log::WriteToConsoleAndLog(line);
				}
				if (info.site == Site::PositionExport) {
					Log::WriteToConsoleAndLog("NaN trace output interpretation: position float32 words, not a converted image/MRT format\n");
				}
			}
		}
	}
}

} // namespace Libs::Graphics::ShaderRecompiler::NanTrace
