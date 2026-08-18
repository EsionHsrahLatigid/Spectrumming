#pragma once

#include "../core/SpectrummingCore.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace spectrumming::plugin
{
class LatestFrameExchange final
{
public:
    bool publish(const core::LumaFrame& frame) noexcept
    {
        int target = -1;
        for(int candidate = 0; candidate < static_cast<int>(slots.size()); ++candidate)
        {
            auto expected = SlotState::free;
            if(slots[static_cast<std::size_t>(candidate)].state.compare_exchange_strong(
                   expected, SlotState::writing, std::memory_order_acq_rel))
            {
                target = candidate;
                break;
            }
        }
        if(target < 0)
            return false;

        auto& targetSlot = slots[static_cast<std::size_t>(target)];
        targetSlot.frame = frame;
        targetSlot.version = nextVersion++;
        targetSlot.state.store(SlotState::ready, std::memory_order_release);

        const auto replaced = publishedIndex.exchange(target, std::memory_order_acq_rel);
        if(replaced >= 0)
        {
            auto expected = SlotState::ready;
            slots[static_cast<std::size_t>(replaced)].state.compare_exchange_strong(
                expected, SlotState::free, std::memory_order_acq_rel);
        }
        return true;
    }

    bool consumeLatest(core::LumaFrame& frame, std::uint64_t& version) noexcept
    {
        const auto index = publishedIndex.exchange(-1, std::memory_order_acq_rel);
        if(index < 0)
            return false;

        auto& slot = slots[static_cast<std::size_t>(index)];
        auto expected = SlotState::ready;
        if(! slot.state.compare_exchange_strong(expected, SlotState::reading,
                                                std::memory_order_acq_rel))
            return false;

        frame = slot.frame;
        version = slot.version;
        slot.state.store(SlotState::free, std::memory_order_release);
        return true;
    }

private:
    enum class SlotState : std::uint8_t
    {
        free,
        writing,
        ready,
        reading
    };

    struct Slot final
    {
        core::LumaFrame frame;
        std::uint64_t version = 0;
        std::atomic<SlotState> state { SlotState::free };
    };

    std::array<Slot, 3> slots;
    std::atomic<int> publishedIndex { -1 };
    std::uint64_t nextVersion = 1;
};
} // namespace spectrumming::plugin
